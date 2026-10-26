"""USB key retrieval, saved Windows Wi-Fi provisioning, and authenticated C6 OTA.

Secrets are read into memory and never printed. Keep the key file private.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time
import urllib.request
import serial

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_KEY = ROOT / 'diagnostics/gateway-private/credentials.json'

def request(url, path, data=None, key=None, content_type='application/json', timeout=30):
    headers = {'Content-Type': content_type}
    if key:
        headers['X-Gladiator-Key'] = key
    req = urllib.request.Request(url.rstrip('/') + path, data=data, headers=headers)
    with urllib.request.urlopen(req, timeout=timeout) as response:
        return json.load(response)

def usb(args):
    connection = serial.Serial(None, 115200, timeout=0.5, write_timeout=2)
    connection.dtr = False
    connection.rts = False
    connection.port = args.port
    with connection:
        deadline = time.monotonic() + 25
        next_command = 0
        while time.monotonic() < deadline:
            if time.monotonic() > next_command:
                connection.write(b'\ncredentials\nstatus\n')
                next_command = time.monotonic() + 2
            line = connection.readline().decode('utf-8', 'replace').strip()
            start = line.find('{')
            if start < 0:
                continue
            try:
                data = json.loads(line[start:])
            except ValueError:
                continue
            if data.get('gatewayProvisioning'):
                args.key_file.parent.mkdir(parents=True, exist_ok=True)
                args.key_file.write_text(json.dumps(data, indent=2), encoding='utf-8')
                print('Maintenance key saved privately to', args.key_file)
                return
    raise RuntimeError('No USB provisioning record received')

def saved_wifi(profile):
    result = subprocess.run(['netsh', 'wlan', 'show', 'profile', f'name={profile}', 'key=clear'], capture_output=True, text=True, check=True)
    match = re.search(r'^\s*Key Content\s*:\s*(.+)$', result.stdout, re.MULTILINE)
    if not match:
        raise RuntimeError('Saved profile password unavailable; use the gateway setup page')
    return match.group(1).strip()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['usb-key', 'provision', 'ota', 'ping', 'status'])
    parser.add_argument('--port', default='COM30')
    parser.add_argument('--url', default='http://192.168.8.1')
    parser.add_argument('--key-file', type=Path, default=DEFAULT_KEY)
    parser.add_argument('--profile', default='Nothing But Net')
    parser.add_argument('--firmware', type=Path, default=ROOT / 'gateway/.pio/build/gladiator_c6/firmware.bin')
    args = parser.parse_args()
    if args.action == 'usb-key':
        usb(args)
        return
    if args.action == 'status':
        print(json.dumps(request(args.url, '/api/status'), indent=2))
        return
    key = json.loads(args.key_file.read_text(encoding='utf-8'))['adminKey']
    if args.action == 'provision':
        payload = json.dumps({'ssid': args.profile, 'password': saved_wifi(args.profile)}).encode()
        print(request(args.url, '/api/config', payload, key))
    elif args.action == 'ping':
        print(request(args.url, '/api/ping', b'{}', key))
    elif args.action == 'ota':
        before = request(args.url, '/api/robot')
        print(request(args.url, '/api/ota', args.firmware.read_bytes(), key, 'application/octet-stream', 120))
        deadline = time.monotonic() + 70
        while time.monotonic() < deadline:
            time.sleep(2)
            try:
                after = request(args.url, '/api/robot', timeout=3)
            except (OSError, ValueError):
                continue
            if after['gateway']['bootId'] != before['gateway']['bootId'] and after['gateway']['uptimeMs'] > 16000:
                assert after['gateway']['otaSlot'] != before['gateway']['otaSlot'], 'OTA slot did not change'
                if before.get('robot') and after.get('robot'):
                    assert before['gateway']['peerBootId'] == after['gateway']['peerBootId'], 'S3 boot changed during C6 OTA'
                    assert after['robot']['uptimeUs'] > before['robot']['uptimeUs'], 'S3 uptime reset'
                print('C6 rebooted into', after['gateway']['otaSlot'], 'and remained online past boot verification.')
                print('S3 boot unchanged.' if before.get('robot') and after.get('robot') else 'S3 continuity not checked: no live S3 telemetry.')
                out = ROOT / 'diagnostics/gateway-ota-verification.json'
                out.write_text(json.dumps({'before': before, 'after': after}, indent=2), encoding='utf-8')
                return
        raise RuntimeError('C6 OTA reboot not verified')

if __name__ == '__main__':
    main()
