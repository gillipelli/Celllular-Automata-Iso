"""Stop subsequent model calls after repeated quota/authentication failures."""
import argparse
import json
from pathlib import Path
import re
import time


def main():
    parser=argparse.ArgumentParser();parser.add_argument('stage_root');args=parser.parse_args()
    root=Path(args.stage_root)
    while not (root/'aggregate.json').exists():
        failures=[]
        for path in root.glob('*/*/events.jsonl'):
            for number,line in enumerate(path.read_text().splitlines()):
                try:event=json.loads(line)
                except json.JSONDecodeError:continue  # An in-progress appended line.
                if event.get('kind')=='provider_error' and re.search(r'\b(401|402|403|429)\b',event.get('error','')):
                    failures.append({'episode':path.parent.name,'line':number+1,'error':event['error']})
        fatal=any(re.search(r'\b(401|402|403)\b',failure['error']) for failure in failures)
        if fatal or len(failures)>=2:
            root.mkdir(parents=True,exist_ok=True)
            (root/'STOP').touch()
            (root/'automatic-stop.json').write_text(json.dumps({'reason':'Authentication/payment failure or repeated quota failures; prevent further dispatch.','failures':failures},indent=2)+'\n')
            print('Cooperative stop requested after account/payment or repeated quota failure.',flush=True)
            return
        time.sleep(1)

if __name__=='__main__':main()
