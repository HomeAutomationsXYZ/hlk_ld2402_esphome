"""Validate and generate C++ for both examples using local components and dummy secrets."""
from pathlib import Path
import subprocess
import sys
import yaml
import base64
import secrets as random_secrets

root = Path(__file__).resolve().parents[1]
build = root / '.build'
build.mkdir(exist_ok=True)

class Loader(yaml.SafeLoader):
    pass

secrets = {
    'api_encryption_key': base64.b64encode(random_secrets.token_bytes(32)).decode(),
    'ota_password': 'test-password', 'wifi_ssid': 'test-network',
    'wifi_password': 'test-password', 'fallback_password': 'test-password',
}
Loader.add_constructor('!secret', lambda loader, node: secrets[loader.construct_scalar(node)])

for variant in ('minimal', 'complete'):
    config = yaml.load((root / f'hlk_ld2402_{variant}.yaml').read_text(), Loader=Loader)
    config['external_components'] = [{'source': {'type': 'local', 'path': str(root / 'components')}}]
    config['esphome']['name'] = f'test-{variant}'
    config['esphome']['build_path'] = str(build / f'esphome-{variant}')
    config.setdefault('esp32', {'board': 'esp32dev'})
    config.setdefault('logger', {'baud_rate': 0})
    config.setdefault('wifi', {'ssid': 'test-network', 'password': 'test-password'})
    path = build / f'{variant}.yaml'
    path.write_text(yaml.safe_dump(config, sort_keys=False))
    subprocess.run([sys.executable, '-m', 'esphome', 'compile', str(path), '--only-generate'], check=True, cwd=root)
