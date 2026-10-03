from __future__ import annotations
import math
from typing import Annotated
import numpy as np
from pydantic import BaseModel, ConfigDict, Field, field_validator

class KingdomAction(BaseModel):
    model_config = ConfigDict(extra='forbid', allow_inf_nan=False)
    allocation: Annotated[list[float], Field(min_length=8, max_length=8, description='Relative weights normalized to sum1: wood, stone, grain, construction, military, monitoring, openness, reserve. Not currency amounts.')]
    construction: Annotated[list[float], Field(min_length=13, max_length=13, description='Relative probabilities for one attempted building per macro step in catalog order. Index0 means no new building; dependencies and stocks constrain success.')]
    expansion_direction: Annotated[int, Field(strict=True, ge=0, le=8)] = 8
    military_target: Annotated[int, Field(strict=True, ge=-1)] = -1
    treaty_partner: Annotated[int, Field(strict=True, ge=-1)] = -1
    treaty_action: Annotated[int, Field(strict=True, ge=0, le=5)] = 5
    quarantine: Annotated[float, Field(ge=0, le=1)] = 0
    trade: Annotated[float, Field(ge=0, le=1)] = 1

    @field_validator('allocation', 'construction')
    @classmethod
    def weights(cls, values):
        if any(not math.isfinite(v) or v < 0 for v in values) or sum(values) <= 0:
            raise ValueError('weights must be finite, nonnegative and have positive total')
        return values

    def vector(self, kingdoms: int, own_id: int | None = None):
        for target in (self.military_target, self.treaty_partner):
            if target >= kingdoms or (own_id is not None and target == own_id):
                raise ValueError('target must be another existing kingdom or -1')
        # Normalize before float32 conversion, avoiding overflow for large finite weights.
        def normalized(values):
            maximum = max(values)
            scaled = [v / maximum for v in values]
            return [v / sum(scaled) for v in scaled]
        return np.asarray(normalized(self.allocation) + normalized(self.construction) + [
            self.expansion_direction, self.military_target, self.treaty_partner,
            self.treaty_action, self.quarantine, self.trade], dtype=np.float32)

    @classmethod
    def balanced(cls):
        return cls(allocation=[1, 1, 3, 2, 1, 1, 1, 1], construction=[0, 4, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1])
