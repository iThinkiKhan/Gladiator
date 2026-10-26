"""Read-only USB bench capture. Never sends a motor/arming command."""
import argparse
import json
import time
from pathlib import Path
import serial

parser=argparse.ArgumentParser()
parser.add_argument('--port',default='COM25')
parser.add_argument('--seconds',type=float,default=30)
parser.add_argument('--output',type=Path,default=Path('diagnostics/sensor-bench.jsonl'))
parser.add_argument('--require',action='append',default=['power'],choices=['power','imu','tofFront','presence'])
args=parser.parse_args()
args.output.parent.mkdir(parents=True,exist_ok=True)
port=serial.Serial()
port.port=args.port; port.baudrate=115200; port.timeout=0.2
port.dtr=False; port.rts=False
port.open()
samples=[]
try:
    deadline=time.monotonic()+args.seconds
    next_request=0
    with args.output.open('w') as output:
        while time.monotonic()<deadline:
            if time.monotonic()>=next_request:
                port.write(b'sensors\n'); next_request=time.monotonic()+1
            line=port.readline().decode('utf-8',errors='replace').strip()
            if not line.startswith('{'): continue
            sample=json.loads(line)
            output.write(json.dumps(sample)+'\n'); output.flush()
            assert sample['controller']['state']=='SAFE', 'Controller unexpectedly armed'
            assert sample['controller']['leftPercent']==sample['controller']['rightPercent']==0
            assert sample['controller']['sensorTaskStarted'] and sample['bus']['started']
            samples.append(sample)
finally:
    port.close()
assert len(samples)>=max(2,int(args.seconds*0.5)),f'Only {len(samples)} snapshots received'
for name in args.require:
    last=samples[-1][name]['meta']
    assert last['online'] and last['hasSample'],f'{name}: {last}'
    assert last['health'] in ('OK','DEGRADED'),f'{name}: {last}'
    assert last['updateCount']>samples[0][name]['meta']['updateCount'],f'{name} counter stalled'
for sample in samples:
    power=sample['power']
    if power['raw'] is not None:
        assert abs(power['derived']['busVolts']-power['raw']['busCounts']*0.00125)<0.00001
        assert abs(power['derived']['currentAmps']-power['raw']['shuntCounts']*0.00125)<0.00001
        assert power['meta']['ageMs']<250
summary={name:{k:samples[-1][name]['meta'][k] for k in ('health','rateHz','updateCount','errorCount')} for name in ('power','imu','tofFront','presence')}
print(json.dumps({'snapshots':len(samples),'sensors':summary,'controller':samples[-1]['controller'],'output':str(args.output)},indent=2))
