"""Spatial summaries derived only from existing permitted observation tensors."""
CHANNELS = ['own_population','own_claim','own_military','supply','infection_fraction',
 'visible_rival_population','visible_rival_claim','visible_rival_military','refugees',
 'trees','fire','stone','fertility','moisture','elevation','own_structure',
 'visible_rival_structure','own_functional_structure','coverage','dry_forest']

def inspect_region(env, kingdom, region='local', x=16, y=16, width=16, height=16):
    if region not in ('local','global') or any(type(v) is not int for v in (x,y,width,height)):
        raise ValueError('region must be local/global and bounds integer')
    if not 1 <= width <= 16 or not 1 <= height <= 16 or not 0 <= x <= 48-width or not 0 <= y <= 48-height:
        raise ValueError('region must be inside 48x48 observation and at most 16x16')
    tensor=env.observe()[kingdom,:2*20*48*48].reshape(2,20,48,48)
    selected=tensor[0 if region=='local' else 1,:,y:y+height,x:x+width]
    return {'region':region,'bounds':[x,y,width,height],
            'channels':{name:{'mean':float(values.mean()),'maximum':float(values.max())}
                        for name,values in zip(CHANNELS,selected)}}
