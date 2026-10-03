"""Provider contract and explicit-cost live adapter; fixture is NOT an LLM."""
from dataclasses import dataclass
import json
import os
import math
from pathlib import Path

@dataclass
class ModelTurn:
    content: list[dict]

class FixtureClient:
    label = 'scripted_tool_fixture'
    def complete(self, messages, tools):
        from .schemas import KingdomAction
        last = messages[-1]
        if not isinstance(last['content'], list):
            return ModelTurn([dict(type='tool_use', id='inspect', name='inspect_kingdom', input={})])
        return ModelTurn([dict(type='tool_use', id='submit', name='submit_action',
            input={'action': KingdomAction.balanced().model_dump(), 'intent': 'Maintain balanced production (scripted fixture).'})])

class AnthropicClient:
    label = 'anthropic'
    def __init__(self, model, spend_cap, input_per_million, output_per_million, max_tokens=1500):
        if not model or any(not math.isfinite(v) or v <= 0 for v in (spend_cap,input_per_million,output_per_million)):
            raise ValueError('Live calls require model, positive spend cap, and explicit current token rates')
        if type(max_tokens) is not int or max_tokens <= 0:
            raise ValueError('max_tokens must be a positive integer')
        if not os.environ.get('ANTHROPIC_API_KEY'):
            raise ValueError('ANTHROPIC_API_KEY is required')
        import anthropic
        self.client = anthropic.Anthropic(max_retries=0, timeout=60)
        self.model, self.cap = model, spend_cap
        self.input_rate, self.output_rate = input_per_million, output_per_million
        self.max_tokens, self.spent, self.calls = max_tokens, 0., 0
        self.usage = []
        self.ledger = None

    def bind_ledger(self, path):
        self.ledger = Path(path)
        if self.ledger.exists():
            saved = json.loads(self.ledger.read_text())
            settings = self.settings()
            if saved['settings'] != settings:
                raise ValueError('live resume requires matching model, rates and spend ceiling')
            self.spent, self.usage = saved['spent'], saved['usage']
        self.persist()

    def settings(self):
        return dict(model=self.model,cap=self.cap,input_rate=self.input_rate,
                    output_rate=self.output_rate,max_tokens=self.max_tokens)

    def persist(self):
        if self.ledger is not None:
            temporary = self.ledger.with_suffix('.tmp')
            temporary.write_text(json.dumps(dict(settings=self.settings(),spent=self.spent,usage=self.usage)))
            with temporary.open('rb') as handle:
                os.fsync(handle.fileno())
            os.replace(temporary,self.ledger)

    def complete(self, messages, tools):
        if self.ledger is None:
            raise ValueError('bind a persistent budget ledger before live calls')
        # Conservative byte-based bound includes message and schema framing overhead.
        bound = len(json.dumps([messages, tools]).encode()) + 4096
        maximum = (bound * self.input_rate + self.max_tokens * self.output_rate) / 1e6
        if self.spent + maximum > self.cap:
            raise RuntimeError('spend ceiling prevents next request')
        # Reserve cost before dispatch: ambiguous network failures must not free budget.
        self.spent += maximum
        self.persist()
        response = self.client.messages.create(model=self.model, max_tokens=self.max_tokens,
                                               messages=messages, tools=tools)
        usage = response.usage
        actual = (usage.input_tokens * self.input_rate + usage.output_tokens * self.output_rate) / 1e6
        self.spent += actual - maximum
        self.calls += 1
        self.usage.append({'input_tokens': usage.input_tokens, 'output_tokens': usage.output_tokens,
                           'estimated_cost': actual})
        self.persist()
        return ModelTurn([block.model_dump(exclude_none=True) for block in response.content])
