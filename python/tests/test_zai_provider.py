import json
from types import SimpleNamespace
import pytest

import ashfall_agents.zai_provider as zai

TOOLS = [{'name':'inspect','description':'Inspect evidence','input_schema':{'type':'object'}}]
MESSAGES = [{'role':'user','content':'Inspect this'}]


def response(arguments='{}'):
    return {'choices':[{'finish_reason':'tool_calls','message':{'role':'assistant','content':None,
        'reasoning_content':'private model trace\n保留', 'tool_calls':[{'id':'call-7','type':'function',
        'function':{'name':'inspect','arguments':arguments}}]}}],
        'usage':{'prompt_tokens':100,'completion_tokens':50}}


class Reply:
    def __init__(self, value): self.value=value
    def __enter__(self): return self
    def __exit__(self,*args): pass
    def read(self): return json.dumps(self.value).encode()


def test_http_protocol_roundtrip(monkeypatch):
    monkeypatch.setenv('ZAI_API_KEY','test-placeholder')
    requests=[]
    def fake(request, timeout):
        requests.append((json.loads(request.data), timeout, request.full_url))
        return Reply(response())
    monkeypatch.setattr(zai,'urlopen',fake)
    body=zai.request_body('glm-5.3','high',MESSAGES,TOOLS,1500)
    result=zai.send_request(body,7.)
    assert result['input_tokens']==100 and result['output_tokens']==50
    assert requests[0][0]['thinking']=={'type':'enabled','clear_thinking':False}
    assert requests[0][0]['tools'][0]['function']['parameters']=={'type':'object'}
    assert requests[0][1:]==(7.,zai.ENDPOINT)
    messages=MESSAGES + [{'role':'assistant','content':[
        {'type':'reasoning','reasoning_content':result['reasoning_content']},
        {'type':'tool_use','id':'call-7','name':'inspect','input':{}}]},
        {'role':'user','content':[{'type':'tool_result','tool_use_id':'call-7','content':'{"ok":true}'}]}]
    zai.send_request(zai.request_body('glm-5.3','low',messages,TOOLS,1500),7.)
    wire=requests[1][0]['messages']
    assert wire[1]['reasoning_content']==result['reasoning_content']
    assert wire[1]['tool_calls'][0]['id']=='call-7'
    assert wire[2]=={'role':'tool','tool_call_id':'call-7','content':'{"ok":true}'}
    assert len(wire[1]['tool_calls'])==1


@pytest.mark.parametrize('arguments',['{broken','[]','{"a":NaN}','{"a":1e999}'])
def test_malformed_arguments_rejected(arguments):
    with pytest.raises(ValueError): zai.parse_response(response(arguments))


def test_usage_required_and_truncation_rejected():
    payload=response();payload['usage']['completion_tokens']=-1
    with pytest.raises(ValueError):zai.parse_response(payload)
    payload=response();payload['choices'][0]['finish_reason']='length'
    with pytest.raises(ValueError):zai.parse_response(payload)


def test_top_level_reasoning_and_unknown_blocks():
    value=zai.chat_messages([{'role':'assistant','reasoning_content':'verbatim\n',
        'content':[{'type':'tool_use','id':'x','name':'inspect','input':{}}]}])
    assert value[0]['reasoning_content']=='verbatim\n'
    with pytest.raises(ValueError):
        zai.chat_messages([{'role':'assistant','content':[{'type':'unexpected','name':'inspect'}]}])


def test_timeout_and_malformed_json(monkeypatch):
    monkeypatch.setenv('ZAI_API_KEY','test-placeholder')
    def timeout(*args,**kwargs):raise TimeoutError('timed out')
    monkeypatch.setattr(zai,'urlopen',timeout)
    with pytest.raises(TimeoutError):zai.send_request({},1.)
    class Malformed(Reply):
        def read(self):return b'not json'
    monkeypatch.setattr(zai,'urlopen',lambda *a,**kw:Malformed(None))
    with pytest.raises(ValueError):zai.send_request({},1.)


def test_durable_reservation_survives_timeout(monkeypatch,tmp_path):
    monkeypatch.setenv('ZAI_API_KEY','test-placeholder')
    ledger=tmp_path/'usage.json'
    client=zai.ZaiClient(spend_cap=1.,input_per_million=1.,output_per_million=1.)
    client.bind_ledger(ledger)
    def fail(body,timeout):
        assert json.loads(ledger.read_text())['spent']>0
        raise TimeoutError('ambiguous request')
    monkeypatch.setattr(zai,'send_request',fail)
    with pytest.raises(TimeoutError):client.complete(MESSAGES,TOOLS)
    reserved=client.spent
    assert reserved>0
    restored=zai.ZaiClient(spend_cap=1.,input_per_million=1.,output_per_million=1.)
    restored.bind_ledger(ledger)
    assert restored.spent==reserved
    monkeypatch.setattr(zai,'send_request',lambda *a:zai.parse_response(response()))
    turn=restored.complete(MESSAGES,TOOLS)
    assert restored.spent==pytest.approx(reserved+.00015)
    assert turn.content[0]['type']=='reasoning'
    assert turn.content[1]['id']=='call-7'
    assert restored.usage[-1]['estimated_cost']==pytest.approx(.00015)


def test_budget_fails_before_network(monkeypatch,tmp_path):
    monkeypatch.setenv('ZAI_API_KEY','test-placeholder')
    client=zai.ZaiClient(spend_cap=.000001,input_per_million=1.,output_per_million=1.)
    client.bind_ledger(tmp_path/'usage.json')
    monkeypatch.setattr(zai,'send_request',lambda *a:pytest.fail('request escaped ceiling'))
    with pytest.raises(RuntimeError):client.complete(MESSAGES,TOOLS)
    assert client.spent==0
    for value in (float('inf'),float('nan'),-1):
        with pytest.raises(ValueError):zai.ZaiClient(spend_cap=value,input_per_million=1.,output_per_million=1.)
