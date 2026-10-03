from dataclasses import dataclass, field

@dataclass
class StrategyMemory:
    records: list[dict] = field(default_factory=list)

    def recall(self, episode: str, kingdom: int, generation: int, query: str = '', limit: int = 5):
        matching = [r for r in self.records if r['episode'] == episode and
                    r['kingdom'] == kingdom and r['generation'] == generation]
        # Internal scope paths can contain evaluator seed/run identifiers. Redact
        # before both searching and returning, avoiding a query hit/miss oracle.
        public_fields = {'kingdom', 'generation', 'tick', 'observation', 'action',
                         'intent', 'outcome', 'generation_changed'}
        matching = [{key: value for key, value in row.items() if key in public_fields}
                    for row in matching]
        if query:
            matching = [r for r in matching if query.lower() in str(r).lower()]
        return matching[-max(0, min(limit, 20)):] if limit > 0 else []

    def record(self, episode, kingdom, before, after, action, intent):
        self.records.append(dict(episode=episode, kingdom=kingdom,
            generation=before['generation'], tick=before['tick'],
            observation=before, action=action, intent=intent, outcome=after,
            generation_changed=before['generation'] != after['generation']))
