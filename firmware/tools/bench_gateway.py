"""Read-only gateway health bench; optional authenticated ping tests the return path."""
import argparse
import json
from pathlib import Path
import time
from gateway_manage import request, DEFAULT_KEY, ROOT

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--ping', action='store_true')
    parser.add_argument('--details', action='store_true', help='Request bounded native snapshots for the bench')
    parser.add_argument('--key-file', type=Path, default=DEFAULT_KEY)
    args = parser.parse_args()
    first = request(args.url, '/api/robot')
    if args.ping or args.details:
        key = json.loads(args.key_file.read_text())['adminKey']
    if args.ping:
        request(args.url, '/api/ping', b'{}', key)
    if args.details:
        request(args.url, '/api/subscribe', b'{"intervalMs":1000,"leaseMs":60000}', key)
    snapshots = []
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline:
        s = request(args.url, '/api/robot', timeout=4)
        snapshots.append(s)
        g, r = s['gateway'], s['robot']
        assert g['robotConnected'] and not g['telemetryStale'], 'S3 telemetry stale'
        assert g['peerBootId'] == first['gateway']['peerBootId'], 'S3 rebooted'
        assert g['bootId'] == first['gateway']['bootId'], 'C6 rebooted'
        assert r['controller']['state'] == 'SAFE', 'Controller is not SAFE'
        assert r['controller']['leftPercent'] == r['controller']['rightPercent'] == 0, 'Drive output active'
        assert r['power']['meta']['online'] and r['power']['meta']['ageMs'] < 500, 'INA stale'
        assert r['imu']['meta']['online'] and r['imu']['meta']['ageMs'] < 300, 'IMU stale'
        native = s.get('native') if s.get('schemaVersion') == 2 else r
        if native:
            assert native['bus']['sda'] == 8 and native['bus']['scl'] == 9 and native['bus']['clockHz'] == 400000
            assert len(native['imu']['reports']) == 10, 'Missing native IMU reports'
            assert abs(native['power']['derived']['busVolts'] - native['power']['raw']['busCounts'] * .00125) < .0001
        time.sleep(.5)
    last = snapshots[-1]
    if args.details:
        assert last.get('native') and last['nativeAgeMs'] < 3000, 'Requested native detail unavailable'
    for counter in ['frameErrors', 'jsonErrors', 'sequenceGaps']:
        assert last['gateway'][counter] == first['gateway'][counter], f'{counter} increased'
    assert last['robot']['power']['meta']['updateCount'] > first['robot']['power']['meta']['updateCount']
    assert last['robot']['imu']['meta']['updateCount'] > first['robot']['imu']['meta']['updateCount']
    if args.ping:
        assert last['gateway']['pongs'] > first['gateway']['pongs'], 'S3 did not answer ping'
    output = ROOT / 'diagnostics/gateway-bench.json'
    output.write_text(json.dumps(snapshots, indent=2), encoding='utf-8')
    print(json.dumps({'samples': len(snapshots), 'seconds': args.seconds, 'ip': last['gateway']['ip'],
        'framesReceived': last['gateway']['rxFrames'] - first['gateway']['rxFrames'],
        'maxTelemetryAgeMs': max(s['gateway']['telemetryAgeMs'] for s in snapshots),
        'pongs': last['gateway']['pongs'], 'powerHz': last['robot']['power']['meta']['rateHz'],
        'imuReportsHz': last['robot']['imu']['meta']['rateHz'], 'controller': last['robot']['controller'],
        'result': 'PASS'}, indent=2))

if __name__ == '__main__':
    main()
