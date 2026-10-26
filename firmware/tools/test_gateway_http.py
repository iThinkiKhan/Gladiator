"""Check that unauthenticated maintenance and wrong-project OTA are rejected."""
import argparse
import json
import urllib.error
from gateway_manage import request, DEFAULT_KEY, ROOT

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    args = parser.parse_args()
    key = json.loads(DEFAULT_KEY.read_text())['adminKey']
    before = request(args.url, '/api/status')
    checks = []
    for path in ['/api/ping', '/api/config', '/api/ota', '/api/control', '/api/subscribe', '/api/link']:
        try:
            request(args.url, path, b'{}', '0' * 32)
            raise AssertionError('Unauthorized maintenance accepted')
        except urllib.error.HTTPError as error:
            assert error.code == 401
            checks.append({'path': path, 'wrongKeyStatus': error.code})
    # Only a header-sized S3 image is sent: this must fail project validation
    # before erasing or writing an OTA slot.
    wrong_image = (ROOT / '.pio/build/gladiator_s3/firmware.bin').read_bytes()[:512]
    try:
        request(args.url, '/api/ota', wrong_image, key, 'application/octet-stream')
        raise AssertionError('S3 image accepted by C6')
    except urllib.error.HTTPError as error:
        assert error.code == 400
        checks.append({'path': '/api/ota', 'wrongProjectStatus': error.code})
    after = request(args.url, '/api/status')
    assert before['bootId'] == after['bootId'] and before['otaSlot'] == after['otaSlot']
    assert not after['otaUpdating']
    result = {'checks': checks, 'bootAndSlotUnchanged': True, 'result': 'PASS'}
    (ROOT / 'diagnostics/gateway-http-tests.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))

if __name__ == '__main__':
    main()
