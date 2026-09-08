"""Shared-actor PPO with a centralized joint-observation critic.

This is an executable training baseline, not a claim of convergence. Rollouts
are synchronous macro steps; masks prevent GAE crossing death/reset boundaries.
"""
import argparse
from pathlib import Path
import json
import numpy as np
import torch
from torch import nn
from torch.distributions import Beta, Categorical, Dirichlet
from ashfall_env import VecWorld


class Model(nn.Module):
    def __init__(self, agents):
        super().__init__()
        self.agents = agents
        self.conv = nn.Sequential(nn.Conv2d(20, 16, 5, 3), nn.ReLU(),
                                  nn.Conv2d(16, 32, 3, 2), nn.ReLU(),
                                  nn.AdaptiveAvgPool2d(1), nn.Flatten())
        self.encoder = nn.Sequential(nn.Linear(128, 128), nn.Tanh())
        self.actor = nn.Linear(128, 8+13+9+(agents+1)*2+6+4)
        self.critic = nn.Sequential(nn.Linear(agents*128, 128), nn.Tanh(), nn.Linear(128, agents))

    def features(self, observation):
        shape = observation.shape[:-1]
        spatial = observation[..., :-64].reshape(-1, 20, 48, 48)
        spatial = self.conv(spatial).reshape(*shape, 64)
        return self.encoder(torch.cat((spatial, observation[..., -64:]), dim=-1))

    def evaluate(self, observations, actions=None):
        # [environments, agents, observation]
        features = self.features(observations)
        raw = self.actor(features)
        values = self.critic(features.flatten(start_dim=1))
        offset = 0
        distributions = []
        for size in (8, 13):
            distributions.append(Dirichlet(nn.functional.softplus(raw[..., offset:offset+size])+1))
            offset += size
        for size in (9, self.agents+1, self.agents+1, 6):
            distributions.append(Categorical(logits=raw[..., offset:offset+size]))
            offset += size
        shapes = nn.functional.softplus(raw[..., offset:offset+4])+1
        distributions.extend((Beta(shapes[..., 0], shapes[..., 1]), Beta(shapes[..., 2], shapes[..., 3])))
        samples = []
        logprob = torch.zeros_like(values)
        entropy = torch.zeros_like(values)
        cursor = 0
        for index, distribution in enumerate(distributions):
            width = 8 if index==0 else 13 if index==1 else 1
            if actions is None:
                sample = distribution.sample()
            else:
                sample = actions[..., cursor:cursor+width] if width>1 else actions[..., cursor]
                if 2 <= index <= 5:
                    if index in (3, 4):
                        sample = sample+1
                    sample = sample.long()
            logprob = logprob+distribution.log_prob(sample)
            entropy = entropy+distribution.entropy()
            if index in (3, 4):
                sample = sample-1
            samples.append(sample if width>1 else sample.unsqueeze(-1))
            cursor += width
        return torch.cat(samples, dim=-1), logprob, entropy, values


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', type=Path)
    parser.add_argument('--library')
    parser.add_argument('--envs', type=int, default=4)
    parser.add_argument('--iterations', type=int, default=100)
    parser.add_argument('--rollout', type=int, default=32)
    parser.add_argument('--epochs', type=int, default=4)
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--out', type=Path, default=Path('runs/training'))
    parser.add_argument('--resume', type=Path)
    args = parser.parse_args()
    if min(args.envs, args.iterations, args.rollout, args.epochs)<1:
        parser.error('counts must be positive')
    torch.set_num_threads(1)
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    config = args.config.read_text() if args.config else '[world]\nsize=64\nforest_warmup=100\nepisode_ticks=2000\n'
    checkpoint = torch.load(args.resume, map_location='cpu', weights_only=True) if args.resume else None
    if checkpoint is not None and args.config is None:
        config=checkpoint['config']
    envs = VecWorld(config, args.envs, args.seed, library=args.library)
    try:
        obs = envs.reset(args.seed)
        agents = obs.shape[1]
        model = Model(agents)
        optimizer = torch.optim.Adam(model.parameters(), lr=3e-4)
        if checkpoint is not None:
            model.load_state_dict(checkpoint['model'])
            optimizer.load_state_dict(checkpoint['optimizer'])
        args.out.mkdir(parents=True, exist_ok=True)
        # Macro discount gamma_tick**K, using the environment's configured K.
        import tomllib
        interval = tomllib.loads(config).get('world', {}).get('macro_interval', 16)
        discount = .999**interval
        reset_counter = args.seed+args.envs
        for iteration in range(args.iterations):
            history = []
            for _ in range(args.rollout):
                observation = torch.from_numpy(obs)
                with torch.no_grad():
                    action, logprob, _, value = model.evaluate(observation)
                following, reward, terminated, truncated, _ = envs.step(action.numpy())
                terminal = terminated | truncated[:, None]
                history.append((observation, action, logprob, value, torch.tensor(reward, dtype=torch.float32), torch.tensor(terminal)))
                for e in range(args.envs):
                    if truncated[e] or terminated[e].all():
                        following[e] = envs.envs[e].reset(reset_counter)[0]
                        reset_counter += 1
                obs = following
            with torch.no_grad():
                bootstrap = model.evaluate(torch.from_numpy(obs))[3]
            advantage = torch.zeros_like(bootstrap)
            advantages, returns = [], []
            for _, _, _, value, reward, terminal in reversed(history):
                mask = (~terminal).float()
                delta = reward+discount*bootstrap*mask-value
                advantage = delta+discount*.95*mask*advantage
                advantages.append(advantage)
                returns.append(advantage+value)
                bootstrap = value
            observations = torch.cat([h[0] for h in history])
            actions = torch.cat([h[1] for h in history])
            old_logprob = torch.cat([h[2] for h in history])
            advantages = torch.cat(list(reversed(advantages)))
            targets = torch.cat(list(reversed(returns)))
            advantages = (advantages-advantages.mean())/(advantages.std(unbiased=False)+1e-8)
            for _ in range(args.epochs):
                _, logprob, entropy, value = model.evaluate(observations, actions)
                ratio = (logprob-old_logprob).exp()
                policy_loss = -torch.minimum(ratio*advantages, ratio.clamp(.8, 1.2)*advantages).mean()
                value_loss = (value-targets).square().mean()
                loss = policy_loss+.5*value_loss-.001*entropy.mean()
                if not torch.isfinite(loss):
                    raise FloatingPointError('non-finite PPO loss')
                optimizer.zero_grad()
                loss.backward()
                nn.utils.clip_grad_norm_(model.parameters(), .5)
                optimizer.step()
            stats = {'iteration': iteration, 'loss': loss.item(),
                     'mean_reward': torch.stack([h[4] for h in history]).mean().item()}
            print(json.dumps(stats), flush=True)
            with (args.out/'metrics.jsonl').open('a') as stream:
                stream.write(json.dumps(stats)+'\n')
            torch.save({'model': model.state_dict(), 'optimizer': optimizer.state_dict(),
                        'agents': agents, 'config': config, 'seed': args.seed}, args.out/'latest.pt')
    finally:
        envs.close()


if __name__ == '__main__':
    main()
