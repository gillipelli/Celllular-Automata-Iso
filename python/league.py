"""Persistent checkpoint pool with prioritized fictitious self-play sampling.

Ratings are empirical win fractions supplied by evaluations, not training loss.
Use this utility to manage opponents for experiments; no convergence claim.
"""
import argparse
import json
from pathlib import Path
import random
import shutil


class League:
    def __init__(self, directory):
        self.directory=Path(directory)
        self.directory.mkdir(parents=True,exist_ok=True)
        self.index=self.directory/'league.json'
        self.members=json.loads(self.index.read_text()) if self.index.exists() else []

    def add(self, checkpoint):
        name=f'policy-{len(self.members):05}.pt'
        shutil.copyfile(checkpoint,self.directory/name)
        self.members.append({'checkpoint':name,'wins':0,'games':0})
        self.save()
        return len(self.members)-1

    def sample(self, seed=1):
        if not self.members:
            raise ValueError('league is empty')
        weights=[(1-(m['wins']+1)/(m['games']+2))**2 for m in self.members]
        index=random.Random(seed).choices(range(len(weights)),weights=weights)[0]
        return index,self.directory/self.members[index]['checkpoint']

    def record(self,index,learner_won):
        member=self.members[index]
        member['games']+=1
        member['wins']+=int(learner_won)
        self.save()

    def save(self):
        temporary=self.index.with_suffix('.tmp')
        temporary.write_text(json.dumps(self.members,indent=2)+'\n')
        temporary.replace(self.index)


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('directory',type=Path)
    parser.add_argument('--add',type=Path)
    parser.add_argument('--sample',type=int)
    args=parser.parse_args()
    league=League(args.directory)
    if args.add is not None:print(league.add(args.add))
    if args.sample is not None:print(league.sample(args.sample))
