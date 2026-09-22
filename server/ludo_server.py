"""LUDO Team Server — servidor colaborativo para LAN, VPN ou cloud.

Python 3.10+, sem dependências externas. Para uso pela Internet, execute atrás
 de um proxy HTTPS (Caddy/Nginx/Traefik) e mantenha a porta interna fechada.
"""
from contextlib import contextmanager
import argparse
import base64
import copy
import hashlib
import hmac
import ipaddress
import json
import secrets
import socket
import sqlite3
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

PROTOCOL = 1
TEAM_PROTOCOL = 9
SESSION_SECONDS = 24 * 60 * 60
PRESENCE_SECONDS = 90
ROOM_OPERATION_LIMIT = 2000
ROOM_RETURN_LIMIT = 800
ROOM_BATCH_LIMIT = 400
ROOM_CHECKPOINT_OPERATIONS = 200
ROOM_CHECKPOINT_SECONDS = 300
ASSET_OPERATION_LIMIT = 4000
ASSET_RETURN_LIMIT = 1200
ASSET_BATCH_LIMIT = 300
MAX_ASSET_SIZE = 64 * 1024 * 1024
PASSWORD_ROUNDS = 250000
MAX_BODY = 128 * 1024 * 1024


class ApiError(Exception):
    def __init__(self, status, message):
        self.status, self.message = status, message


def encode(value):
    return json.dumps(value, ensure_ascii=False, separators=(',', ':'), sort_keys=True,
                      allow_nan=False).encode('utf-8')


def _validate_map(item, ids=None):
    if not isinstance(item, dict):
        raise ApiError(400, 'Mapa inválido.')
    identifier = item.get('id')
    if (not isinstance(identifier, str) or not identifier or len(identifier) > 128
            or identifier.startswith('$')):
        raise ApiError(400, 'Identificação de mapa inválida.')
    if ids is not None:
        if identifier in ids:
            raise ApiError(400, 'Identificação de mapa repetida.')
        ids.add(identifier)
    item.pop('activeLayerIdx', None)
    item.pop('activeLayerId', None)
    return identifier


def _portable_asset_path(value):
    if not isinstance(value, str):
        return None
    normalized = value.replace('\\', '/').strip()
    while normalized.startswith('./'):
        normalized = normalized[2:]
    parts = [part for part in normalized.split('/') if part not in ('', '.')]
    if not parts or parts[0].lower() != 'assets' or any(part == '..' for part in parts):
        return None
    return '/'.join(parts)


def strip_local_paths(value):
    """Remove machine-local paths while preserving portable Assets/... links."""
    if isinstance(value, dict):
        for key in list(value):
            if key == 'collapsed':
                value.pop(key, None)
            elif key in ('source', 'sourcePath', 'panoramaPath'):
                portable = _portable_asset_path(value.get(key))
                if portable is None:
                    value.pop(key, None)
                else:
                    value[key] = portable
            else:
                strip_local_paths(value[key])
    elif isinstance(value, list):
        for child in value:
            strip_local_paths(child)
    return value


def clean_payload(payload):
    if not isinstance(payload, dict) or payload.get('format') != 'LudoMapProject':
        raise ApiError(400, 'Formato de projeto inválido.')
    if payload.get('formatVersion') != 3:
        raise ApiError(400, 'Esta versão do servidor aceita projetos no formato 3.')
    payload = copy.deepcopy(payload)
    for field in ('activeMapDocIdx', 'rpgMakerProjectRoot', 'assetDatabase',
                  'assetReferences', 'editorVersion'):
        payload.pop(field, None)
    maps = payload.get('maps')
    if not isinstance(maps, list) or not maps or len(maps) > 10000:
        raise ApiError(400, 'O projeto precisa ter entre 1 e 10000 mapas.')
    ids = set()
    for item in maps:
        _validate_map(item, ids)

    # Caminhos locais do anfitrião nunca devem ser reutilizados em outro PC.
    strip_local_paths(payload)
    return payload


def resources(payload):
    return {k: v for k, v in payload.items() if k != 'maps'}


def maps_by_id(payload):
    return {m['id']: m for m in payload.get('maps', [])}


def map_order(payload):
    return [m['id'] for m in payload.get('maps', [])]


def make_delta(base, target):
    """Return only fields/maps that changed between two complete snapshots."""
    base_maps, target_maps = maps_by_id(base), maps_by_id(target)
    delta = {}
    changed = [copy.deepcopy(m) for m in target.get('maps', [])
               if base_maps.get(m['id']) != m]
    deleted = [identifier for identifier in map_order(base)
               if identifier not in target_maps]
    if changed:
        delta['maps'] = changed
    if deleted:
        delta['deletedMaps'] = deleted
    if map_order(base) != map_order(target):
        delta['mapOrder'] = map_order(target)
    if resources(base) != resources(target):
        delta['resources'] = copy.deepcopy(resources(target))
    return delta


def _find_layer(nodes, layer_id):
    if not isinstance(nodes, list):
        return None
    for node in nodes:
        if not isinstance(node, dict):
            continue
        if node.get('id') == layer_id:
            return node
        found = _find_layer(node.get('children', []), layer_id)
        if found is not None:
            return found
    return None


def _replace_layer(nodes, layer_id, replacement):
    if not isinstance(nodes, list):
        return False
    for index, node in enumerate(nodes):
        if not isinstance(node, dict):
            continue
        if node.get('id') == layer_id:
            if not isinstance(replacement, dict) or replacement.get('id') != layer_id:
                raise ApiError(400, 'Camada colaborativa inválida.')
            nodes[index] = copy.deepcopy(replacement)
            return True
        if _replace_layer(node.get('children', []), layer_id, replacement):
            return True
    return False


def _validate_asset_entry(entry):
    if not isinstance(entry, dict):
        raise ApiError(400, 'Recurso compartilhado inválido.')
    asset_id = entry.get('id')
    path = _portable_asset_path(entry.get('path'))
    blob_hash = entry.get('sha256')
    size = entry.get('size')
    if not isinstance(asset_id, str) or not asset_id or len(asset_id) > 160:
        raise ApiError(400, 'Identificação do recurso inválida.')
    if path is None or len(path) > 1000:
        raise ApiError(400, 'Caminho do recurso inválido.')
    if (not Store._valid_blob_hash(blob_hash) or type(size) is not int
            or size < 0 or size > MAX_ASSET_SIZE):
        raise ApiError(400, 'Arquivo do recurso inválido.')
    metadata = entry.get('metadata', {})
    if not isinstance(metadata, dict):
        raise ApiError(400, 'Metadados do recurso inválidos.')
    normalized = {
        'id': asset_id, 'path': path, 'sha256': blob_hash, 'size': size,
        'type': str(entry.get('type', 'other'))[:40],
        'category': str(entry.get('category', ''))[:120],
        'metadata': copy.deepcopy(metadata),
    }
    return normalized


def _validate_asset_operation(operation):
    if not isinstance(operation, dict):
        raise ApiError(400, 'Operação de recurso inválida.')
    op_id = operation.get('id')
    kind = operation.get('kind')
    client = operation.get('client', '')
    if not isinstance(op_id, str) or not op_id or len(op_id) > 180:
        raise ApiError(400, 'Identificação da operação de recurso inválida.')
    if kind not in ('asset.upsert', 'asset.delete'):
        raise ApiError(400, 'Tipo de operação de recurso não suportado.')
    if not isinstance(client, str) or len(client) > 128:
        raise ApiError(400, 'Cliente de recurso inválido.')
    if kind == 'asset.upsert':
        return op_id, kind, client, _validate_asset_entry(operation.get('asset'))
    asset_id = operation.get('assetId')
    if not isinstance(asset_id, str) or not asset_id or len(asset_id) > 160:
        raise ApiError(400, 'Identificação do recurso removido inválida.')
    return op_id, kind, client, asset_id


def _validate_room_operation(operation):
    if not isinstance(operation, dict):
        raise ApiError(400, 'Operação colaborativa inválida.')
    op_id = operation.get('id')
    map_id = operation.get('map')
    kind = operation.get('kind')
    client = operation.get('client', '')
    if not isinstance(op_id, str) or not op_id or len(op_id) > 160:
        raise ApiError(400, 'Identificação de operação inválida.')
    if not isinstance(map_id, str) or not map_id or len(map_id) > 128 or map_id.startswith('$'):
        raise ApiError(400, 'Mapa da operação inválido.')
    if kind not in ('tile.cells', 'region.cells', 'objects.patch', 'layer.replace', 'map.replace', 'map.properties', 'map.create', 'map.meta', 'map.delete', 'map.order'):
        raise ApiError(400, 'Tipo de operação colaborativa não suportado.')
    if not isinstance(client, str) or len(client) > 128:
        raise ApiError(400, 'Cliente colaborativo inválido.')
    return op_id, map_id, kind, client


def apply_room_operation(payload, operation, current_seq):
    """Apply one small collaborative operation to the live project state."""
    _op_id, map_id, kind, _client = _validate_room_operation(operation)
    project_maps = maps_by_id(payload)
    target_map = project_maps.get(map_id)

    if kind == 'map.create':
        if target_map is not None:
            raise ApiError(409, 'Este mapa já existe na sala.')
        replacement = copy.deepcopy(operation.get('value'))
        if not isinstance(replacement, dict) or replacement.get('id') != map_id:
            raise ApiError(400, 'Novo mapa colaborativo inválido.')
        _validate_map(replacement)
        strip_local_paths(replacement)
        payload.setdefault('maps', []).append(replacement)
        return

    if target_map is None:
        raise ApiError(404, 'Mapa da sala não encontrado.')

    if kind == 'map.properties':
        changes = operation.get('value')
        if not isinstance(changes, dict) or not changes or set(changes) - {'map', 'reflectionSettings'}:
            raise ApiError(400, 'Propriedades de mapa inválidas.')
        candidate = copy.deepcopy(target_map)
        for key, value in changes.items():
            if not isinstance(value, dict):
                raise ApiError(400, 'Propriedades devem ser objetos.')
            if key == 'map':
                for dimension in ('width', 'height', 'tileWidth', 'tileHeight'):
                    if dimension in value and value[dimension] != target_map.get('map', {}).get(dimension):
                        raise ApiError(409, 'Redimensionar exige atualização estrutural do mapa.')
                props = candidate.setdefault('map', {})
                for field, item in value.items():
                    if item is None:
                        props.pop(field, None)
                    else:
                        props[field] = copy.deepcopy(item)
            else:
                candidate[key] = copy.deepcopy(value)
        _validate_map(candidate)
        strip_local_paths(candidate)
        target_map.update(candidate)
        return

    if kind == 'map.meta':
        meta = operation.get('value')
        if not isinstance(meta, dict):
            raise ApiError(400, 'Metadados do mapa inválidos.')
        name = meta.get('name', '')
        parent_id = meta.get('parentId', '')
        variation_base = meta.get('variationBaseId', '')
        variation_name = meta.get('variationName', '')
        rpg_map_id = meta.get('rpgMakerMapId', 0)
        imported = meta.get('rpgMakerImported', False)
        if (not isinstance(name, str) or not name.strip() or len(name) > 300
                or not isinstance(parent_id, str) or len(parent_id) > 128
                or not isinstance(variation_base, str) or len(variation_base) > 128
                or not isinstance(variation_name, str) or len(variation_name) > 300
                or type(rpg_map_id) is not int or rpg_map_id < 0 or rpg_map_id > 999999
                or type(imported) is not bool or parent_id == map_id or variation_base == map_id):
            raise ApiError(400, 'Metadados do mapa inválidos.')
        target_map['name'] = name.strip()
        for key, value in (('parentId', parent_id), ('variationBaseId', variation_base),
                           ('variationName', variation_name)):
            if value:
                target_map[key] = value
            else:
                target_map.pop(key, None)
        if rpg_map_id > 0:
            target_map['rpgMakerMapId'] = rpg_map_id
        else:
            target_map.pop('rpgMakerMapId', None)
        if imported:
            target_map['rpgMakerImported'] = True
        else:
            target_map.pop('rpgMakerImported', None)
        return

    if kind in ('map.delete', 'map.order'):
        # Operações de projeto são aplicadas pelo room-exchange porque alteram
        # a coleção/ordem de live_maps, não apenas um payload isolado.
        raise ApiError(500, 'Operação estrutural fora do contexto do projeto.')

    if kind == 'tile.cells':
        layer_id = operation.get('layer')
        changes = operation.get('changes')
        if not isinstance(layer_id, str) or not layer_id or not isinstance(changes, list) or len(changes) > 20000:
            raise ApiError(400, 'Alterações de tiles inválidas.')
        layer = _find_layer(target_map.get('layers', []), layer_id)
        if not layer or layer.get('type') != 'tilelayer':
            raise ApiError(409, 'A camada de tiles não existe mais.')
        rows = layer.get('data2D')
        if not isinstance(rows, list):
            raise ApiError(409, 'A camada de tiles está inconsistente.')
        for change in changes:
            if not isinstance(change, dict):
                raise ApiError(400, 'Célula de tile inválida.')
            x, y = change.get('x'), change.get('y')
            if type(x) is not int or type(y) is not int or y < 0 or y >= len(rows):
                raise ApiError(400, 'Coordenada de tile inválida.')
            row = rows[y]
            if not isinstance(row, list) or x < 0 or x >= len(row):
                raise ApiError(400, 'Coordenada de tile inválida.')
            value = change.get('value')
            if value is not None and not isinstance(value, (dict, list)):
                raise ApiError(400, 'Conteúdo de tile inválido.')
            row[x] = copy.deepcopy(value)
        return

    if kind == 'region.cells':
        changes = operation.get('changes')
        if not isinstance(changes, list) or len(changes) > 20000:
            raise ApiError(400, 'Alterações de regiões inválidas.')
        info = target_map.get('map', {})
        width = int(info.get('width', 0) or 0)
        height = int(info.get('height', 0) or 0)
        regions = target_map.get('rpgMakerRegions')
        if not isinstance(regions, dict):
            regions = {}
            target_map['rpgMakerRegions'] = regions
        target_map['rpgMakerRegionsAuthored'] = True
        for change in changes:
            if not isinstance(change, dict):
                raise ApiError(400, 'Região inválida.')
            x, y, value = change.get('x'), change.get('y'), change.get('value')
            if type(x) is not int or type(y) is not int or type(value) is not int:
                raise ApiError(400, 'Região inválida.')
            if x < 0 or y < 0 or x >= width or y >= height or value < 0 or value > 255:
                raise ApiError(400, 'Região fora dos limites.')
            key = f'{x},{y}'
            if value == 0:
                regions.pop(key, None)
            else:
                regions[key] = value
        return

    if kind == 'objects.patch':
        layer_id = operation.get('layer')
        upsert = operation.get('upsert', [])
        deleted = operation.get('delete', [])
        if (not isinstance(layer_id, str) or not layer_id or not isinstance(upsert, list)
                or not isinstance(deleted, list) or len(upsert) + len(deleted) > 10000):
            raise ApiError(400, 'Alterações de objetos inválidas.')
        layer = _find_layer(target_map.get('layers', []), layer_id)
        if not layer or layer.get('type') != 'objectgroup':
            raise ApiError(409, 'A camada de objetos não existe mais.')
        objects = layer.get('objects')
        if not isinstance(objects, list):
            objects = []
        by_id = {item.get('id'): copy.deepcopy(item) for item in objects
                 if isinstance(item, dict) and isinstance(item.get('id'), str) and item.get('id')}
        for object_id in deleted:
            if not isinstance(object_id, str):
                raise ApiError(400, 'Objeto removido inválido.')
            by_id.pop(object_id, None)
        order = [item.get('id') for item in objects if isinstance(item, dict) and item.get('id') in by_id]
        for item in upsert:
            if not isinstance(item, dict):
                raise ApiError(400, 'Objeto inválido.')
            object_id = item.get('id')
            if not isinstance(object_id, str) or not object_id or len(object_id) > 128:
                raise ApiError(400, 'Identificação de objeto inválida.')
            if object_id not in by_id:
                order.append(object_id)
            by_id[object_id] = copy.deepcopy(item)
        layer['objects'] = [by_id[item] for item in order if item in by_id]
        return

    if kind == 'layer.replace':
        layer_id = operation.get('layer')
        replacement = operation.get('value')
        if not isinstance(layer_id, str) or not layer_id:
            raise ApiError(400, 'Camada inválida.')
        if not isinstance(replacement, dict):
            raise ApiError(400, 'Camada colaborativa inválida.')
        replacement = strip_local_paths(copy.deepcopy(replacement))
        if not _replace_layer(target_map.get('layers', []), layer_id, replacement):
            raise ApiError(409, 'A camada foi removida ou reorganizada por outra pessoa.')
        return

    if kind == 'map.replace':
        base_seq = operation.get('baseSeq', current_seq)
        if type(base_seq) is not int or base_seq != current_seq:
            raise ApiError(409, 'A estrutura deste mapa mudou na sala. Receba as alterações antes de substituir a estrutura.')
        replacement = copy.deepcopy(operation.get('value'))
        if not isinstance(replacement, dict) or replacement.get('id') != map_id:
            raise ApiError(400, 'Mapa colaborativo inválido.')
        _validate_map(replacement)
        strip_local_paths(replacement)
        target_map.clear()
        target_map.update(replacement)
        return


def apply_resource_patches(current, operations):
    if not isinstance(operations, list) or len(operations) > 10000:
        raise ApiError(400, 'Operações de recursos inválidas.')
    result = copy.deepcopy(current)
    def valid_segment(key):
        return ((isinstance(key, str) and bool(key)) or
                (isinstance(key, dict) and set(key) == {'id'} and isinstance(key['id'], str) and bool(key['id'])))
    def locate(target, segment):
        if isinstance(segment, str):
            if not isinstance(target, dict):
                raise ApiError(409, 'A estrutura do recurso mudou.')
            return segment, segment in target
        if not isinstance(target, list):
            raise ApiError(409, 'A coleção do recurso mudou.')
        matches = [i for i, item in enumerate(target) if isinstance(item, dict) and item.get('id') == segment['id']]
        if len(matches) > 1:
            raise ApiError(409, 'Identificação de recurso duplicada.')
        return (matches[0], True) if matches else (len(target), False)
    for operation in operations:
        if not isinstance(operation, dict) or operation.get('kind') not in ('resource.set', 'resource.delete'):
            raise ApiError(400, 'Operação de recurso inválida.')
        path = operation.get('path')
        if (not isinstance(path, list) or not path or len(path) > 32
                or not isinstance(path[0], str) or any(not valid_segment(k) for k in path)
                or path[0] in ('maps', 'projectId', 'format', 'formatVersion')):
            raise ApiError(400, 'Caminho de recurso inválido.')
        target = result
        for segment in path[:-1]:
            key, exists = locate(target, segment)
            if not exists:
                raise ApiError(409, 'O recurso foi removido.')
            target = target[key]
        key, exists = locate(target, path[-1])
        expected_exists = operation.get('beforeExists')
        if type(expected_exists) is not bool or (expected_exists and 'before' not in operation):
            raise ApiError(400, 'Base do recurso inválida.')
        desired_exists = operation['kind'] == 'resource.set'
        if desired_exists and 'value' not in operation:
            raise ApiError(400, 'Valor de recurso ausente.')
        if isinstance(path[-1], dict) and desired_exists:
            value = operation['value']
            if not isinstance(value, dict) or value.get('id') != path[-1]['id']:
                raise ApiError(400, 'Identificação do recurso inconsistente.')
        if len(path)>1 and isinstance(path[-2],dict) and path[-1]=='id':
            raise ApiError(400, 'A identificação estável não pode ser alterada.')
        already = exists == desired_exists and (not exists or target[key] == operation['value'])
        if not already and (exists != expected_exists or (exists and target[key] != operation['before'])):
            raise ApiError(409, 'O mesmo recurso foi alterado por outra pessoa.')
        if desired_exists:
            if isinstance(target, list) and not exists:
                target.append(copy.deepcopy(operation['value']))
            else:
                target[key] = copy.deepcopy(operation['value'])
        elif exists:
            if isinstance(target, list):
                target.pop(key)
            else:
                target.pop(key, None)
    return strip_local_paths(result)


def password_hash(password, salt):
    return hashlib.pbkdf2_hmac('sha256', password.encode(), bytes.fromhex(salt), PASSWORD_ROUNDS).hex()


class Store:
    def __init__(self, directory, server_name=None, public_url=None):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True, exist_ok=True)
        self.path = self.directory / 'ludo.sqlite3'
        with self.connect() as db:
            db.executescript('''
            CREATE TABLE IF NOT EXISTS users(name TEXT PRIMARY KEY, salt TEXT NOT NULL, hash TEXT NOT NULL, role TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS sessions(token TEXT PRIMARY KEY, user TEXT NOT NULL, expires REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS projects(id TEXT PRIMARY KEY, name TEXT NOT NULL, revision INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS versions(project TEXT, revision INTEGER, payload BLOB NOT NULL, user TEXT NOT NULL, created REAL NOT NULL, PRIMARY KEY(project,revision));
            CREATE TABLE IF NOT EXISTS metadata(key TEXT PRIMARY KEY, value TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS presence(project TEXT NOT NULL, token TEXT NOT NULL, user TEXT NOT NULL, map TEXT NOT NULL, updated REAL NOT NULL, PRIMARY KEY(project,token));
            CREATE TABLE IF NOT EXISTS live_projects(project TEXT PRIMARY KEY, payload BLOB NOT NULL, updated REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS live_maps(project TEXT NOT NULL, map TEXT NOT NULL, payload BLOB NOT NULL, updated REAL NOT NULL,
                                                 PRIMARY KEY(project,map));
            CREATE TABLE IF NOT EXISTS room_sequences(project TEXT NOT NULL, map TEXT NOT NULL, seq INTEGER NOT NULL, PRIMARY KEY(project,map));
            CREATE TABLE IF NOT EXISTS room_operations(project TEXT NOT NULL, map TEXT NOT NULL, seq INTEGER NOT NULL,
                                                       op_id TEXT NOT NULL, user TEXT NOT NULL, client TEXT NOT NULL,
                                                       payload BLOB NOT NULL, created REAL NOT NULL,
                                                       PRIMARY KEY(project,map,seq), UNIQUE(project,op_id));
            CREATE TABLE IF NOT EXISTS room_checkpoint_state(project TEXT PRIMARY KEY, last_created REAL NOT NULL, ops_since INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS resource_state(project TEXT PRIMARY KEY, seq INTEGER NOT NULL, payload BLOB NOT NULL, user TEXT NOT NULL, updated REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS resource_operations(project TEXT NOT NULL, seq INTEGER NOT NULL, op_id TEXT NOT NULL, user TEXT NOT NULL, client TEXT NOT NULL, payload BLOB NOT NULL, created REAL NOT NULL, PRIMARY KEY(project,seq), UNIQUE(project,op_id));
            CREATE TABLE IF NOT EXISTS asset_blobs(hash TEXT PRIMARY KEY, size INTEGER NOT NULL, payload BLOB NOT NULL, created REAL NOT NULL);
            CREATE TABLE IF NOT EXISTS raster_blob_refs(project TEXT NOT NULL, op_id TEXT NOT NULL, hash TEXT NOT NULL, PRIMARY KEY(project,op_id,hash));
            CREATE TABLE IF NOT EXISTS project_blob_uploads(project TEXT NOT NULL, hash TEXT NOT NULL, created REAL NOT NULL, PRIMARY KEY(project,hash));
            CREATE TABLE IF NOT EXISTS project_assets(project TEXT NOT NULL, asset_id TEXT NOT NULL, path TEXT NOT NULL, hash TEXT NOT NULL, size INTEGER NOT NULL, type TEXT NOT NULL, category TEXT NOT NULL, metadata BLOB NOT NULL, updated REAL NOT NULL, PRIMARY KEY(project,asset_id), UNIQUE(project,path));
            CREATE TABLE IF NOT EXISTS asset_sequences(project TEXT PRIMARY KEY, seq INTEGER NOT NULL);
            CREATE TABLE IF NOT EXISTS asset_operations(project TEXT NOT NULL, seq INTEGER NOT NULL, op_id TEXT NOT NULL, user TEXT NOT NULL, client TEXT NOT NULL, payload BLOB NOT NULL, created REAL NOT NULL, PRIMARY KEY(project,seq), UNIQUE(project,op_id));
            CREATE INDEX IF NOT EXISTS idx_room_operations_project_map_seq ON room_operations(project,map,seq);
            CREATE INDEX IF NOT EXISTS idx_resource_operations_project_seq ON resource_operations(project,seq);
            CREATE INDEX IF NOT EXISTS idx_asset_operations_project_seq ON asset_operations(project,seq);
            ''')
            if not db.execute("SELECT 1 FROM metadata WHERE key='server_id'").fetchone():
                db.execute("INSERT INTO metadata VALUES('server_id',?)", (uuid.uuid4().hex,))
            if not db.execute("SELECT 1 FROM metadata WHERE key='server_name'").fetchone():
                db.execute("INSERT INTO metadata VALUES('server_name','LUDO Team Server')")
            if not db.execute("SELECT 1 FROM metadata WHERE key='public_url'").fetchone():
                db.execute("INSERT INTO metadata VALUES('public_url','')")
            if server_name:
                db.execute("UPDATE metadata SET value=? WHERE key='server_name'", (server_name[:120],))
            if public_url is not None:
                db.execute("UPDATE metadata SET value=? WHERE key='public_url'", (public_url[:500],))
            self._prune(db, time.time())

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.path, timeout=30)
        db.row_factory = sqlite3.Row
        db.execute('PRAGMA foreign_keys=ON')
        db.execute('PRAGMA journal_mode=WAL')
        try:
            with db:
                yield db
        finally:
            db.close()

    def _metadata(self, db, key, default=''):
        row = db.execute('SELECT value FROM metadata WHERE key=?', (key,)).fetchone()
        return row['value'] if row else default

    def info(self, db=None):
        if db is None:
            with self.connect() as connection:
                return self.info(connection)
        return {
            'protocol': PROTOCOL,
            'teamProtocol': TEAM_PROTOCOL,
            'serverName': self._metadata(db, 'server_name', 'LUDO Team Server'),
            'serverId': self._metadata(db, 'server_id'),
            'publicUrl': self._metadata(db, 'public_url'),
            'mode': 'cloud-ready',
        }

    def discovery_info(self, port):
        now = time.time()
        with self.connect() as db:
            # Broadcast acontece a cada 1,5 s. Aqui fazemos somente leituras:
            # a limpeza periódica continua nos requests normais para não criar
            # contenção de escrita no SQLite apenas para atualizar o Hub.
            result = self.info(db)
            result.update({
                'type': 'ludo-team',
                'discoveryVersion': 1,
                'port': int(port),
                'projects': int(db.execute('SELECT COUNT(*) FROM projects').fetchone()[0]),
                'online': int(db.execute('SELECT COUNT(*) FROM presence WHERE updated>=?',
                                         (now - PRESENCE_SECONDS,)).fetchone()[0]),
            })
            return result

    def _prune(self, db, now):
        db.execute('DELETE FROM sessions WHERE expires<?', (now,))
        db.execute('DELETE FROM presence WHERE updated<?', (now - PRESENCE_SECONDS,))
        db.execute('DELETE FROM presence WHERE token NOT IN (SELECT token FROM sessions)')

    def add_user(self, name, password, role):
        name = name.strip()
        if (not name or len(name) > 64 or len(password) < 10 or len(password) > 512
                or role not in ('admin', 'editor', 'viewer')):
            raise ValueError('Informe nome, senha de 10 a 512 caracteres e perfil válido.')
        salt = secrets.token_hex(16)
        with self.connect() as db:
            db.execute('INSERT INTO users VALUES(?,?,?,?)',
                       (name, salt, password_hash(password, salt), role))

    def users(self):
        with self.connect() as db:
            return [dict(row) for row in db.execute('SELECT name,role FROM users ORDER BY name')]

    def remove_user(self, name):
        with self.connect() as db:
            db.execute('BEGIN IMMEDIATE')
            user = db.execute('SELECT role FROM users WHERE name=?', (name,)).fetchone()
            if not user:
                raise ValueError('Usuário não encontrado.')
            if (user['role'] == 'admin'
                    and db.execute("SELECT count(*) FROM users WHERE role='admin'").fetchone()[0] <= 1):
                raise ValueError('Mantenha pelo menos um administrador.')
            db.execute('DELETE FROM presence WHERE user=?', (name,))
            db.execute('DELETE FROM sessions WHERE user=?', (name,))
            db.execute('DELETE FROM users WHERE name=?', (name,))

    def backup(self):
        destination = self.directory / 'backups'
        destination.mkdir(exist_ok=True)
        path = destination / ('ludo-' + time.strftime('%Y%m%d-%H%M%S') + '-'
                              + secrets.token_hex(3) + '.sqlite3')
        with self.connect() as source:
            target = sqlite3.connect(path)
            try:
                source.backup(target)
            finally:
                target.close()
        return path

    @staticmethod
    def _valid_blob_hash(value):
        return (isinstance(value, str) and len(value) == 64
                and all(ch in '0123456789abcdef' for ch in value))

    def _authorize_token(self, db, token):
        digest = hashlib.sha256(token.encode()).hexdigest()
        auth = db.execute('''SELECT s.user,u.role FROM sessions s
                             JOIN users u ON s.user=u.name WHERE s.token=?''',
                          (digest,)).fetchone()
        if not auth:
            raise ApiError(401, 'Sua sessão expirou. Conecte novamente.')
        return digest, auth['user'], auth['role']

    def put_asset_blob(self, project_id, blob_hash, payload, token):
        if not self._valid_blob_hash(blob_hash):
            raise ApiError(400, 'Hash do recurso inválido.')
        if not isinstance(payload, (bytes, bytearray)) or len(payload) > MAX_ASSET_SIZE:
            raise ApiError(413, 'O recurso excede o limite de 64 MB.')
        actual = hashlib.sha256(payload).hexdigest()
        if actual != blob_hash:
            raise ApiError(400, 'O conteúdo recebido não corresponde ao hash informado.')
        now = time.time()
        with self.connect() as db:
            db.execute('BEGIN IMMEDIATE')
            self._prune(db, now)
            _digest, _user, role = self._authorize_token(db, token)
            if role == 'viewer':
                raise ApiError(403, 'Seu acesso permite apenas baixar recursos.')
            if not db.execute('SELECT 1 FROM projects WHERE id=?', (project_id,)).fetchone():
                raise ApiError(404, 'Projeto não encontrado.')
            row = db.execute('SELECT size FROM asset_blobs WHERE hash=?', (blob_hash,)).fetchone()
            if row and int(row['size']) != len(payload):
                raise ApiError(409, 'Já existe um recurso incompatível com este hash.')
            if not row:
                db.execute('INSERT INTO asset_blobs VALUES(?,?,?,?)',
                           (blob_hash, len(payload), sqlite3.Binary(payload), now))
            # A carga física é deduplicada globalmente, mas a autorização de
            # uso permanece por projeto. Saber o SHA de um arquivo de outro
            # projeto não concede acesso ao conteúdo dele.
            db.execute('INSERT OR REPLACE INTO project_blob_uploads VALUES(?,?,?)',
                       (project_id, blob_hash, now))
        return {'ok': True, 'hash': blob_hash, 'size': len(payload)}

    def expand_rasters(self, db, project, op_id, value, budget=None):
        if budget is None:
            budget = [MAX_BODY]
        if isinstance(value, list):
            return [self.expand_rasters(db, project, op_id, v, budget) for v in value]
        if not isinstance(value, dict):
            return value
        if '$raster' in value:
            digest = value['$raster']
            if set(value) != {'$raster'} or not self._valid_blob_hash(digest):
                raise ApiError(400, 'Referência raster inválida.')
            blob = db.execute('SELECT b.payload FROM asset_blobs b JOIN project_blob_uploads u ON u.hash=b.hash WHERE u.project=? AND b.hash=?', (project, digest)).fetchone()
            if not blob:
                raise ApiError(409, 'Envie o arquivo raster antes da operação.')
            raw = bytes(blob['payload'])
            budget[0] -= ((len(raw)+2)//3)*4
            if budget[0] < 0:
                raise ApiError(413, 'Lote de imagens excede o limite de memória.')
            if not raw.startswith(b'\x89PNG\r\n\x1a\n'):
                raise ApiError(400, 'Raster deve ser PNG.')
            db.execute('INSERT OR IGNORE INTO raster_blob_refs VALUES(?,?,?)', (project, op_id, digest))
            return 'data:image/png;base64,' + base64.b64encode(raw).decode('ascii')
        return {k: self.expand_rasters(db, project, op_id, v, budget) for k, v in value.items()}

    def collect_blobs(self, grace_seconds=86400, now=None):
        """Snapshots retain inline PNGs; live operations retain binary references."""
        now = time.time() if now is None else now
        cutoff = now - max(3600, grace_seconds)
        with self.connect() as db:
            db.execute('BEGIN IMMEDIATE')
            db.execute('DELETE FROM raster_blob_refs WHERE NOT EXISTS (SELECT 1 FROM room_operations o WHERE o.project=raster_blob_refs.project AND o.op_id=raster_blob_refs.op_id)')
            rows = db.execute("""SELECT hash FROM asset_blobs b WHERE created<?
                AND NOT EXISTS (SELECT 1 FROM project_assets a WHERE a.hash=b.hash)
                AND NOT EXISTS (SELECT 1 FROM raster_blob_refs r WHERE r.hash=b.hash)
                AND NOT EXISTS (SELECT 1 FROM project_blob_uploads u WHERE u.hash=b.hash AND u.created>=?)""", (cutoff,cutoff)).fetchall()
            for row in rows:
                db.execute('DELETE FROM project_blob_uploads WHERE hash=?', (row['hash'],))
                db.execute('DELETE FROM asset_blobs WHERE hash=?', (row['hash'],))
            return len(rows)

    def get_asset_blob(self, project_id, blob_hash, token):
        if not self._valid_blob_hash(blob_hash):
            raise ApiError(400, 'Hash do recurso inválido.')
        now = time.time()
        with self.connect() as db:
            self._prune(db, now)
            self._authorize_token(db, token)
            if not db.execute('SELECT 1 FROM projects WHERE id=?', (project_id,)).fetchone():
                raise ApiError(404, 'Projeto não encontrado.')
            referenced = db.execute('SELECT 1 FROM project_assets WHERE project=? AND hash=?',
                                    (project_id, blob_hash)).fetchone()
            if not referenced:
                referenced = db.execute('SELECT 1 FROM raster_blob_refs WHERE project=? AND hash=?', (project_id,blob_hash)).fetchone()
            if not referenced:
                raise ApiError(404, 'Este recurso não pertence ao projeto solicitado.')
            row = db.execute('SELECT payload,size FROM asset_blobs WHERE hash=?', (blob_hash,)).fetchone()
            if not row:
                raise ApiError(404, 'Recurso não encontrado no servidor.')
            return bytes(row['payload']), int(row['size'])

    def dispatch(self, method, route, body, token):
        now = time.time()
        with self.connect() as db:
            # O Hub consulta saúde periodicamente. Essa rota é deliberadamente
            # read-only para não disputar BEGIN IMMEDIATE com edição em tempo real.
            if route == ['health'] and method == 'GET':
                result = self.info(db)
                result['name'] = result['serverName']  # compatibilidade do endpoint de saúde antigo
                result['projects'] = int(db.execute('SELECT COUNT(*) FROM projects').fetchone()[0])
                result['online'] = int(db.execute('SELECT COUNT(*) FROM presence WHERE updated>=?',
                                                  (now - PRESENCE_SECONDS,)).fetchone()[0])
                return result
            # Uma transação cobre comparação de versão, lock estrutural e commit.
            db.execute('BEGIN IMMEDIATE')
            self._prune(db, now)
            if route == ['login'] and method == 'POST':
                name, password = body.get('name', ''), body.get('password', '')
                if not isinstance(name, str) or not isinstance(password, str) or len(password) > 512:
                    raise ApiError(400, 'Dados de acesso inválidos.')
                user = db.execute('SELECT * FROM users WHERE name=?', (name,)).fetchone()
                salt = user['salt'] if user else '00' * 16
                candidate = password_hash(password, salt)
                if not user or not hmac.compare_digest(candidate, user['hash']):
                    raise ApiError(401, 'Usuário ou senha incorretos.')
                raw = secrets.token_urlsafe(32)
                digest = hashlib.sha256(raw.encode()).hexdigest()
                db.execute('INSERT INTO sessions VALUES(?,?,?)',
                           (digest, name, now + SESSION_SECONDS))
                result = {'token': raw, 'role': user['role'], 'name': name}
                result.update(self.info(db))
                return result

            digest = hashlib.sha256(token.encode()).hexdigest()
            auth = db.execute('''SELECT s.user,u.role FROM sessions s
                                 JOIN users u ON s.user=u.name WHERE s.token=?''',
                              (digest,)).fetchone()
            if not auth:
                raise ApiError(401, 'Sua sessão expirou. Conecte novamente.')
            user, role = auth['user'], auth['role']

            if route == ['logout'] and method == 'POST':
                db.execute('DELETE FROM presence WHERE token=?', (digest,))
                db.execute('DELETE FROM sessions WHERE token=?', (digest,))
                return {'ok': True}

            if route == ['projects']:
                if method == 'GET':
                    rows = db.execute('''
                        SELECT p.id,p.name,p.revision,
                               COALESCE((SELECT SUM(rs.seq) FROM room_sequences rs WHERE rs.project=p.id),0) AS roomSeqTotal,
                               COALESCE((SELECT r.seq FROM resource_state r WHERE r.project=p.id),0) AS resourceSeq,
                               COALESCE((SELECT a.seq FROM asset_sequences a WHERE a.project=p.id),0) AS assetSeq,
                               COALESCE((SELECT COUNT(*) FROM presence pr WHERE pr.project=p.id AND pr.updated>=?),0) AS online,
                               COALESCE((SELECT lp.updated FROM live_projects lp WHERE lp.project=p.id),0) AS updated
                        FROM projects p ORDER BY p.name
                    ''', (now - PRESENCE_SECONDS,)).fetchall()
                    return {'projects': [dict(r) for r in rows]}
                if method == 'POST':
                    if role != 'admin':
                        raise ApiError(403, 'Somente o administrador publica novos projetos.')
                    payload = clean_payload(body.get('payload'))
                    identifier = uuid.uuid4().hex
                    name = str(payload.get('projectName', 'Projeto'))[:200]
                    db.execute('INSERT INTO projects VALUES(?,?,1)', (identifier, name))
                    db.execute('INSERT INTO versions VALUES(?,1,?,?,?)',
                               (identifier, encode(payload), user, now))
                    db.execute('INSERT INTO live_projects VALUES(?,?,?)',
                               (identifier, encode(payload), now))
                    for map_payload in payload.get('maps', []):
                        db.execute('INSERT OR REPLACE INTO live_maps VALUES(?,?,?,?)',
                                   (identifier, map_payload['id'], encode(map_payload), now))
                    db.execute('INSERT OR REPLACE INTO room_checkpoint_state VALUES(?,?,0)',
                               (identifier, now))
                    db.execute('INSERT OR REPLACE INTO resource_state VALUES(?,?,?,?,?)',
                               (identifier, 0, encode(resources(payload)), user, now))
                    db.execute('INSERT OR REPLACE INTO asset_sequences VALUES(?,0)', (identifier,))
                    return {'id': identifier, 'revision': 1, 'payload': payload,
                            'presence': [], 'roomSeqs': {},
                            'resourceSeq': 0, 'assetSeq': 0}

            if len(route) < 2 or route[0] != 'projects':
                raise ApiError(404, 'Rota não encontrada.')
            identifier = route[1]
            project = db.execute('SELECT * FROM projects WHERE id=?', (identifier,)).fetchone()
            if not project:
                raise ApiError(404, 'Projeto não encontrado.')
            revision = project['revision']

            def read_version(number):
                row = db.execute('SELECT payload FROM versions WHERE project=? AND revision=?',
                                 (identifier, number)).fetchone()
                if not row:
                    raise ApiError(404, 'Versão não encontrada.')
                return json.loads(row['payload'])

            def read_checkpoint():
                row = db.execute('SELECT payload FROM live_projects WHERE project=?',
                                 (identifier,)).fetchone()
                if row:
                    return json.loads(row['payload'])
                payload = read_version(revision)
                db.execute('INSERT OR REPLACE INTO live_projects VALUES(?,?,?)',
                           (identifier, encode(payload), now))
                return payload

            def ensure_resource_state():
                row = db.execute('SELECT seq,payload FROM resource_state WHERE project=?',
                                 (identifier,)).fetchone()
                if row:
                    return int(row['seq']), json.loads(row['payload'])
                payload = resources(read_checkpoint())
                db.execute('INSERT OR REPLACE INTO resource_state VALUES(?,?,?,?,?)',
                           (identifier, 0, encode(payload), user, now))
                return 0, payload

            def resource_seq():
                return ensure_resource_state()[0]

            def asset_seq():
                row = db.execute('SELECT seq FROM asset_sequences WHERE project=?',
                                 (identifier,)).fetchone()
                if row:
                    return int(row['seq'])
                db.execute('INSERT OR REPLACE INTO asset_sequences VALUES(?,0)', (identifier,))
                return 0

            def ensure_live_maps():
                rows = db.execute('SELECT map,payload FROM live_maps WHERE project=?',
                                  (identifier,)).fetchall()
                if rows:
                    return rows
                # One-time migration for databases created before live rooms.
                # or by an early room-protocol build. Afterwards live exchanges read
                # only the requested map rows, never the full project blob.
                checkpoint = read_checkpoint()
                for map_payload in checkpoint.get('maps', []):
                    db.execute('INSERT OR REPLACE INTO live_maps VALUES(?,?,?,?)',
                               (identifier, map_payload['id'], encode(map_payload), now))
                return db.execute('SELECT map,payload FROM live_maps WHERE project=?',
                                  (identifier,)).fetchall()

            def live_map_ids():
                ensure_live_maps()
                return {r['map'] for r in db.execute(
                    'SELECT map FROM live_maps WHERE project=?', (identifier,))}

            def read_live_map(map_id):
                ensure_live_maps()
                row = db.execute('SELECT payload FROM live_maps WHERE project=? AND map=?',
                                 (identifier, map_id)).fetchone()
                return json.loads(row['payload']) if row else None

            def read_current():
                checkpoint = read_checkpoint()
                _resource_seq, live_resources = ensure_resource_state()
                checkpoint_maps = checkpoint.get('maps', [])
                checkpoint = copy.deepcopy(live_resources)
                checkpoint['maps'] = checkpoint_maps
                overlays = {r['map']: json.loads(r['payload']) for r in ensure_live_maps()}
                checkpoint['maps'] = [copy.deepcopy(overlays.get(item.get('id'), item))
                                      for item in checkpoint.get('maps', []) if item.get('id') in overlays]
                # A structural checkpoint can introduce/reorder maps. save_current
                # keeps live_maps aligned, so an extra map here indicates only a
                # migration edge-case; preserve it deterministically.
                known = {item.get('id') for item in checkpoint['maps']}
                for map_id, map_payload in overlays.items():
                    if map_id not in known:
                        checkpoint['maps'].append(copy.deepcopy(map_payload))
                return checkpoint

            def save_current(payload):
                # Real checkpoint/global commits consolidate resources and reset
                # the canonical per-map rows in one transaction. This path is
                # rare; the event-driven room hot path never reaches it.
                db.execute('INSERT OR REPLACE INTO live_projects VALUES(?,?,?)',
                           (identifier, encode(payload), now))
                previous_seq, previous_resources = ensure_resource_state()
                next_resources = resources(payload)
                if previous_resources != next_resources:
                    db.execute('INSERT OR REPLACE INTO resource_state VALUES(?,?,?,?,?)',
                               (identifier, previous_seq + 1, encode(next_resources), user, now))
                db.execute('DELETE FROM live_maps WHERE project=?', (identifier,))
                current_ids = {map_payload['id'] for map_payload in payload.get('maps', [])}
                for map_payload in payload.get('maps', []):
                    db.execute('INSERT INTO live_maps VALUES(?,?,?,?)',
                               (identifier, map_payload['id'], encode(map_payload), now))
                # Removed maps must not keep ghost room cursors/history.
                for row in db.execute('SELECT map FROM room_sequences WHERE project=?', (identifier,)).fetchall():
                    if row['map'] not in current_ids:
                        db.execute('DELETE FROM room_sequences WHERE project=? AND map=?', (identifier, row['map']))
                        db.execute('DELETE FROM room_operations WHERE project=? AND map=?', (identifier, row['map']))
                db.execute('INSERT OR REPLACE INTO room_checkpoint_state VALUES(?,?,0)',
                           (identifier, now))

            def save_live_map(map_payload):
                map_id = map_payload.get('id') if isinstance(map_payload, dict) else None
                if not isinstance(map_id, str) or not map_id:
                    raise ApiError(500, 'Mapa colaborativo inválido no estado ao vivo.')
                db.execute('INSERT OR REPLACE INTO live_maps VALUES(?,?,?,?)',
                           (identifier, map_id, encode(map_payload), now))

            def room_sequences():
                return {r['map']: r['seq'] for r in db.execute(
                    'SELECT map,seq FROM room_sequences WHERE project=?', (identifier,))}

            def room_seq(map_id):
                row = db.execute('SELECT seq FROM room_sequences WHERE project=? AND map=?',
                                 (identifier, map_id)).fetchone()
                return int(row['seq']) if row else 0

            def set_room_seq(map_id, seq):
                db.execute('INSERT OR REPLACE INTO room_sequences VALUES(?,?,?)',
                           (identifier, map_id, int(seq)))

            def presence_rows():
                return [{'user': r['user'], 'map': r['map'], 'mine': r['token'] == digest,
                         'updated': r['updated']}
                        for r in db.execute('''SELECT * FROM presence WHERE project=? AND updated>=?
                                              ORDER BY user''',
                                            (identifier, now - PRESENCE_SECONDS))]

            def touch_presence(map_id):
                current = read_current()
                valid = {m['id'] for m in current['maps']}
                if not isinstance(map_id, str) or map_id not in valid:
                    map_id = ''
                db.execute('INSERT OR REPLACE INTO presence VALUES(?,?,?,?,?)',
                           (identifier, digest, user, map_id, now))

            if len(route) == 2 and method == 'GET':
                return {'id': identifier, 'revision': revision,
                        'payload': read_current(), 'presence': presence_rows(), 'roomSeqs': room_sequences(),
                        'resourceSeq': resource_seq(), 'assetSeq': asset_seq()}

            action = route[2] if len(route) > 2 else ''
            if action == 'status' and method == 'GET':
                return {'revision': revision, 'presence': presence_rows(), 'roomSeqs': room_sequences(),
                        'resourceSeq': resource_seq(), 'assetSeq': asset_seq()}
            if action == 'history' and method == 'GET':
                return {'versions': [dict(r) for r in db.execute(
                    '''SELECT revision,user,created FROM versions WHERE project=?
                       ORDER BY revision DESC LIMIT 200''', (identifier,))]}
            if action == 'versions' and len(route) == 4 and method == 'GET':
                try:
                    number = int(route[3])
                except ValueError:
                    raise ApiError(400, 'Versão inválida.')
                return {'revision': number, 'payload': read_version(number)}

            # Heartbeat também é permitido para visualizadores: presença não edita projeto.
            if action == 'heartbeat' and method == 'POST':
                touch_presence(body.get('map', ''))
                return {'revision': revision, 'presence': presence_rows(), 'roomSeqs': room_sequences()}

            if action == 'sync' and method == 'POST':
                base_number = body.get('revision')
                if type(base_number) is not int or base_number < 1 or base_number > revision:
                    raise ApiError(400, 'Versão de origem inválida.')
                base = read_version(base_number)
                current = read_current()
                return {'revision': revision, 'delta': make_delta(base, current), 'presence': presence_rows(),
                        'roomSeqs': room_sequences()}

            if action == 'room-exchange' and method == 'POST':
                active_map = body.get('map', '')
                cursors = body.get('cursors', {})
                operations = body.get('operations', [])
                force_resets = body.get('resetMaps', [])
                if not isinstance(cursors, dict) or len(cursors) > 256:
                    raise ApiError(400, 'Cursores das salas inválidos.')
                if not isinstance(operations, list) or len(operations) > ROOM_BATCH_LIMIT:
                    raise ApiError(400, 'Muitas operações colaborativas em um único envio.')
                if not isinstance(force_resets, list) or len(force_resets) > 32:
                    raise ApiError(400, 'Muitos mapas solicitados para reparo.')
                if role == 'viewer' and operations:
                    raise ApiError(403, 'Seu acesso permite apenas visualizar a sala.')

                valid_map_ids = live_map_ids()
                if not isinstance(active_map, str) or active_map not in valid_map_ids:
                    active_map = ''
                map_cache = {}
                def room_map(map_id):
                    if map_id not in map_cache:
                        value = read_live_map(map_id)
                        if value is None:
                            raise ApiError(404, 'Mapa da sala não encontrado.')
                        map_cache[map_id] = value
                    return map_cache[map_id]

                db.execute('INSERT OR REPLACE INTO presence VALUES(?,?,?,?,?)',
                           (identifier, digest, user, active_map, now))

                requested_maps = set()
                forced_reset_maps = set()
                normalized_cursors = {}
                for map_id, cursor in cursors.items():
                    if not isinstance(map_id, str) or type(cursor) is not int or cursor < 0:
                        raise ApiError(400, 'Cursor de sala inválido.')
                    if map_id not in valid_map_ids:
                        continue
                    requested_maps.add(map_id)
                    normalized_cursors[map_id] = cursor
                for map_id in force_resets:
                    if not isinstance(map_id, str) or not map_id or len(map_id) > 128:
                        raise ApiError(400, 'Mapa solicitado para reparo inválido.')
                    if map_id in valid_map_ids:
                        requested_maps.add(map_id)
                        forced_reset_maps.add(map_id)
                        normalized_cursors.setdefault(map_id, room_seq(map_id))
                if active_map:
                    requested_maps.add(active_map)
                    normalized_cursors.setdefault(active_map, room_seq(active_map))

                acked = []
                changed_maps = set()
                deleted_maps = set()
                accepted_operations = 0
                structural_changed = False
                requested_order = None

                raster_budget = [MAX_BODY]
                for raw_operation in operations:
                    operation = copy.deepcopy(raw_operation)
                    op_id, map_id, kind, client = _validate_room_operation(operation)
                    duplicate = db.execute(
                        'SELECT map,seq FROM room_operations WHERE project=? AND op_id=?',
                        (identifier, op_id)).fetchone()
                    if duplicate:
                        acked.append(op_id)
                        if map_id in valid_map_ids:
                            requested_maps.add(map_id)
                        continue

                    wire_operation = copy.deepcopy(operation)
                    operation = self.expand_rasters(db, identifier, op_id, operation, raster_budget)
                    if kind == 'map.create':
                        if map_id in valid_map_ids:
                            raise ApiError(409, 'Este mapa já existe na sala.')
                        tiny = {'maps': []}
                        apply_room_operation(tiny, operation, 0)
                        created_map = tiny['maps'][0]
                        map_cache[map_id] = created_map
                        valid_map_ids.add(map_id)
                        requested_maps.add(map_id)
                        normalized_cursors.setdefault(map_id, 0)
                        current_seq = 0
                        structural_changed = True
                        changed_maps.add(map_id)
                    elif kind == 'map.delete':
                        if map_id not in valid_map_ids:
                            acked.append(op_id)
                            continue
                        if len(valid_map_ids) <= 1:
                            raise ApiError(409, 'O projeto precisa manter pelo menos um mapa.')
                        current_seq = room_seq(map_id)
                        valid_map_ids.remove(map_id)
                        deleted_maps.add(map_id)
                        changed_maps.discard(map_id)
                        requested_maps.discard(map_id)
                        forced_reset_maps.discard(map_id)
                        normalized_cursors.pop(map_id, None)
                        map_cache.pop(map_id, None)
                        structural_changed = True
                    elif kind == 'map.order':
                        if map_id not in valid_map_ids:
                            raise ApiError(404, 'O mapa de referência da ordem não existe mais.')
                        order = operation.get('order')
                        if (not isinstance(order, list) or len(order) != len(valid_map_ids)
                                or any(not isinstance(item, str) or not item for item in order)
                                or len(set(order)) != len(order) or set(order) != valid_map_ids):
                            raise ApiError(409, 'A ordem de mapas não corresponde à estrutura atual da equipe.')
                        current_seq = room_seq(map_id)
                        requested_order = list(order)
                        requested_maps.add(map_id)
                        normalized_cursors.setdefault(map_id, int(operation.get('cursor', 0) or 0))
                        structural_changed = True
                    elif kind == 'map.meta':
                        if map_id not in valid_map_ids:
                            raise ApiError(404, 'O mapa de uma alteração estrutural não existe mais.')
                        meta = operation.get('value') if isinstance(operation.get('value'), dict) else {}
                        parent_id = meta.get('parentId', '')
                        variation_base = meta.get('variationBaseId', '')
                        if parent_id and parent_id not in valid_map_ids:
                            raise ApiError(409, 'O mapa pai não existe mais na equipe.')
                        if variation_base and variation_base not in valid_map_ids:
                            raise ApiError(409, 'O mapa-base da variação não existe mais na equipe.')
                        current_seq = room_seq(map_id)
                        apply_room_operation({'maps': [room_map(map_id)]}, operation, current_seq)
                        requested_maps.add(map_id)
                        normalized_cursors.setdefault(map_id, int(operation.get('cursor', 0) or 0))
                        changed_maps.add(map_id)
                        structural_changed = True
                    else:
                        if map_id not in valid_map_ids:
                            raise ApiError(404, 'O mapa de uma alteração colaborativa não existe mais.')
                        requested_maps.add(map_id)
                        normalized_cursors.setdefault(map_id, int(operation.get('cursor', 0) or 0))
                        current_seq = room_seq(map_id)
                        apply_room_operation({'maps': [room_map(map_id)]}, operation, current_seq)
                        changed_maps.add(map_id)

                    next_seq = current_seq + 1
                    set_room_seq(map_id, next_seq)
                    db.execute('''INSERT INTO room_operations(project,map,seq,op_id,user,client,payload,created)
                                  VALUES(?,?,?,?,?,?,?,?)''',
                               (identifier, map_id, next_seq, op_id, user, client,
                                encode(wire_operation), now))
                    acked.append(op_id)
                    accepted_operations += 1

                    cutoff = next_seq - ROOM_OPERATION_LIMIT
                    if cutoff > 0:
                        db.execute('DELETE FROM room_operations WHERE project=? AND map=? AND seq<=?',
                                   (identifier, map_id, cutoff))

                # Validação estrutural FINAL do lote. Validar map.meta apenas no
                # instante em que ele chega não basta: um map.delete posterior
                # no mesmo request poderia deixar parentId/variationBaseId
                # apontando para um mapa removido. Como todo o endpoint roda em
                # uma transação, qualquer erro aqui desfaz o lote inteiro.
                if structural_changed:
                    final_maps = {}
                    for structural_map_id in valid_map_ids:
                        value = map_cache.get(structural_map_id)
                        if value is None:
                            value = read_live_map(structural_map_id)
                        if value is None:
                            raise ApiError(409, 'A estrutura mudou durante a atualização.')
                        final_maps[structural_map_id] = value

                    for structural_map_id, structural_map in final_maps.items():
                        parent_id = structural_map.get('parentId', '')
                        variation_base = structural_map.get('variationBaseId', '')
                        if not isinstance(parent_id, str) or not isinstance(variation_base, str):
                            raise ApiError(409, 'A hierarquia de mapas ficou inválida.')
                        if (parent_id and parent_id not in final_maps) or (variation_base and variation_base not in final_maps):
                            raise ApiError(409, 'Uma alteração deixaria referências para mapas removidos.')
                        if parent_id == structural_map_id or variation_base == structural_map_id:
                            raise ApiError(409, 'Um mapa não pode apontar para si mesmo.')

                    # Ciclos de pasta e de variação são rejeitados antes do
                    # checkpoint, evitando que dois clientes válidos criem uma
                    # estrutura impossível quando suas operações se combinam.
                    for field, label in (('parentId', 'hierarquia'), ('variationBaseId', 'variações')):
                        for start in final_maps:
                            seen = set()
                            current = start
                            while current:
                                if current in seen:
                                    raise ApiError(409, f'Foi detectado um ciclo na {label} dos mapas.')
                                seen.add(current)
                                current = final_maps.get(current, {}).get(field, '')

                for changed_map_id in changed_maps:
                    if changed_map_id in valid_map_ids:
                        save_live_map(room_map(changed_map_id))
                for deleted_map_id in deleted_maps:
                    db.execute('DELETE FROM live_maps WHERE project=? AND map=?',
                               (identifier, deleted_map_id))

                if structural_changed:
                    shell = read_current()
                    if requested_order is not None:
                        by_id = maps_by_id(shell)
                        if set(requested_order) != set(by_id):
                            raise ApiError(409, 'A ordem de mapas mudou durante a atualização.')
                        shell['maps'] = [copy.deepcopy(by_id[item]) for item in requested_order]
                    db.execute('INSERT OR REPLACE INTO live_projects VALUES(?,?,?)',
                               (identifier, encode(shell), now))

                if changed_maps or structural_changed:
                    checkpoint_state = db.execute(
                        'SELECT last_created,ops_since FROM room_checkpoint_state WHERE project=?',
                        (identifier,)).fetchone()
                    if checkpoint_state:
                        checkpoint_last = float(checkpoint_state['last_created'])
                        checkpoint_ops = int(checkpoint_state['ops_since']) + accepted_operations
                    else:
                        checkpoint_last = now
                        checkpoint_ops = accepted_operations
                    checkpoint_due = (structural_changed or checkpoint_ops >= ROOM_CHECKPOINT_OPERATIONS or
                                      (checkpoint_ops > 0 and now - checkpoint_last >= ROOM_CHECKPOINT_SECONDS))
                    if checkpoint_due:
                        checkpoint_payload = read_current()
                        revision += 1
                        db.execute('INSERT INTO versions VALUES(?,?,?,?,?)',
                                   (identifier, revision, encode(checkpoint_payload), 'Equipe', now))
                        db.execute('UPDATE projects SET revision=?,name=? WHERE id=?',
                                   (revision, str(checkpoint_payload.get('projectName', 'Projeto'))[:200], identifier))
                        save_current(checkpoint_payload)
                    else:
                        db.execute('INSERT OR REPLACE INTO room_checkpoint_state VALUES(?,?,?)',
                                   (identifier, checkpoint_last, checkpoint_ops))

                valid_map_ids = live_map_ids()
                requested_maps.intersection_update(valid_map_ids)
                forced_reset_maps.intersection_update(valid_map_ids)

                response_ops = []
                reset_maps = []
                response_seqs = {}
                for map_id in sorted(requested_maps):
                    latest = room_seq(map_id)
                    response_seqs[map_id] = latest
                    if map_id in forced_reset_maps:
                        reset_maps.append(copy.deepcopy(room_map(map_id)))
                        continue
                    cursor = normalized_cursors.get(map_id, latest)
                    if cursor > latest:
                        cursor = latest
                    if latest - cursor > ROOM_RETURN_LIMIT:
                        reset_maps.append(copy.deepcopy(room_map(map_id)))
                        continue
                    rows = db.execute('''SELECT seq,user,client,payload FROM room_operations
                                         WHERE project=? AND map=? AND seq>?
                                         ORDER BY seq LIMIT ?''',
                                      (identifier, map_id, cursor, ROOM_RETURN_LIMIT + 1)).fetchall()
                    if len(rows) > ROOM_RETURN_LIMIT:
                        reset_maps.append(copy.deepcopy(room_map(map_id)))
                        continue
                    for row in rows:
                        response_ops.append({'map': map_id, 'seq': row['seq'], 'user': row['user'],
                                             'client': row['client'],
                                             'operation': json.loads(row['payload'])})

                return {'revision': revision, 'acked': acked, 'operations': response_ops,
                        'resetMaps': reset_maps, 'roomSeqs': response_seqs,
                        'resourceSeq': resource_seq(), 'assetSeq': asset_seq(), 'presence': presence_rows()}

            if action == 'resources-exchange' and method == 'POST':
                cursor = body.get('cursor', 0)
                if type(cursor) is not int or cursor < 0:
                    raise ApiError(400, 'Cursor de recursos inválido.')
                current_seq, current_resources = ensure_resource_state()
                incoming = body.get('resources')
                patches = body.get('operations')
                if incoming is not None and patches is not None:
                    raise ApiError(400, 'Envie snapshot ou operações, não ambos.')
                conflict = False
                acked = []
                if incoming is not None or patches is not None:
                    if role == 'viewer':
                        raise ApiError(403, 'Seu acesso permite apenas visualizar recursos.')
                    if patches is None and not isinstance(incoming, dict):
                        raise ApiError(400, 'Recursos globais inválidos.')
                    base_seq = body.get('baseSeq')
                    op_id = body.get('id')
                    client = body.get('client', '')
                    if type(base_seq) is not int or base_seq < 0:
                        raise ApiError(400, 'Versão de recursos inválida.')
                    if not isinstance(op_id, str) or not op_id or len(op_id) > 180:
                        raise ApiError(400, 'Identificação da alteração de recursos inválida.')
                    if not isinstance(client, str) or len(client) > 128:
                        raise ApiError(400, 'Cliente de recursos inválido.')
                    duplicate = db.execute('SELECT seq FROM resource_operations WHERE project=? AND op_id=?',
                                           (identifier, op_id)).fetchone()
                    if duplicate:
                        acked.append(op_id)
                    else:
                        normalized = None
                        if patches is not None:
                            try:
                                normalized = apply_resource_patches(current_resources, patches)
                            except ApiError as error:
                                if error.status != 409:
                                    raise
                                conflict = True
                        elif base_seq != current_seq:
                            conflict = True
                        else:
                            normalized = strip_local_paths(copy.deepcopy(incoming))
                    if not duplicate and not conflict:
                        if normalized.get('projectId') not in (None, read_current().get('projectId')):
                            raise ApiError(409, 'Os recursos pertencem a outro projeto.')
                        next_seq = current_seq + 1
                        db.execute('INSERT OR REPLACE INTO resource_state VALUES(?,?,?,?,?)',
                                   (identifier, next_seq, encode(normalized), user, now))
                        event = {'id': op_id, 'kind': 'resources.patch' if patches is not None else 'resources.replace', 'client': client,
                                 'operations': patches} if patches is not None else {'id': op_id, 'kind': 'resources.replace', 'resources': normalized}
                        db.execute('''INSERT INTO resource_operations(project,seq,op_id,user,client,payload,created)
                                      VALUES(?,?,?,?,?,?,?)''',
                                   (identifier, next_seq, op_id, user, client, encode(event), now))
                        cutoff = next_seq - ASSET_OPERATION_LIMIT
                        if cutoff > 0:
                            db.execute('DELETE FROM resource_operations WHERE project=? AND seq<=?',
                                       (identifier, cutoff))
                        current_seq, current_resources = next_seq, normalized
                        acked.append(op_id)

                        # Recursos globais são raros, então viram checkpoint recuperável.
                        # O mapa ao vivo continua em live_maps; não recriamos suas linhas.
                        checkpoint_payload = read_current()
                        revision += 1
                        db.execute('INSERT INTO versions VALUES(?,?,?,?,?)',
                                   (identifier, revision, encode(checkpoint_payload), user, now))
                        db.execute('UPDATE projects SET revision=?,name=? WHERE id=?',
                                   (revision, str(checkpoint_payload.get('projectName', 'Projeto'))[:200], identifier))
                        db.execute('INSERT OR REPLACE INTO live_projects VALUES(?,?,?)',
                                   (identifier, encode(checkpoint_payload), now))
                        db.execute('INSERT OR REPLACE INTO room_checkpoint_state VALUES(?,?,0)',
                                   (identifier, now))

                response = {'revision': revision, 'resourceSeq': current_seq, 'acked': acked,
                            'conflict': conflict, 'assetSeq': asset_seq(), 'presence': presence_rows()}
                if conflict or cursor < current_seq or patches is not None:
                    response['resources'] = copy.deepcopy(current_resources)
                return response

            if action == 'asset-blobs-check' and method == 'POST':
                hashes = body.get('hashes', [])
                if not isinstance(hashes, list) or len(hashes) > 1000:
                    raise ApiError(400, 'Lista de recursos inválida.')
                normalized = []
                for blob_hash in hashes:
                    if not Store._valid_blob_hash(blob_hash):
                        raise ApiError(400, 'Hash do recurso inválido.')
                    normalized.append(blob_hash)
                existing = set()
                if normalized:
                    placeholders = ','.join('?' for _ in normalized)
                    params = [identifier] + normalized
                    existing = {r['hash'] for r in db.execute(
                        'SELECT b.hash FROM asset_blobs b JOIN project_blob_uploads p ON p.hash=b.hash '
                        'WHERE p.project=? AND b.hash IN (' + placeholders + ')', params)}
                return {'missing': [item for item in normalized if item not in existing]}

            if action == 'asset-exchange' and method == 'POST':
                cursor = body.get('cursor', 0)
                operations = body.get('operations', [])
                if type(cursor) is not int or cursor < 0:
                    raise ApiError(400, 'Cursor de assets inválido.')
                if not isinstance(operations, list) or len(operations) > ASSET_BATCH_LIMIT:
                    raise ApiError(400, 'Muitas alterações de assets em um único envio.')
                if role == 'viewer' and operations:
                    raise ApiError(403, 'Seu acesso permite apenas baixar recursos.')

                current_seq = asset_seq()
                acked = []
                for raw in operations:
                    operation = copy.deepcopy(raw)
                    op_id, kind, client, value = _validate_asset_operation(operation)
                    duplicate = db.execute('SELECT seq FROM asset_operations WHERE project=? AND op_id=?',
                                           (identifier, op_id)).fetchone()
                    if duplicate:
                        acked.append(op_id)
                        continue
                    if kind == 'asset.upsert':
                        asset = value
                        blob = db.execute('''SELECT b.size FROM asset_blobs b
                                             JOIN project_blob_uploads p ON p.hash=b.hash
                                             WHERE p.project=? AND b.hash=?''',
                                          (identifier, asset['sha256'])).fetchone()
                        if not blob or int(blob['size']) != asset['size']:
                            raise ApiError(409, 'Envie o arquivo do recurso para este projeto antes de publicar o manifesto.')
                        collision = db.execute('SELECT asset_id FROM project_assets WHERE project=? AND path=?',
                                               (identifier, asset['path'])).fetchone()
                        if collision and collision['asset_id'] != asset['id']:
                            raise ApiError(409, 'Outro recurso já usa este caminho no projeto.')
                        db.execute('''INSERT OR REPLACE INTO project_assets
                                      (project,asset_id,path,hash,size,type,category,metadata,updated)
                                      VALUES(?,?,?,?,?,?,?,?,?)''',
                                   (identifier, asset['id'], asset['path'], asset['sha256'], asset['size'],
                                    asset['type'], asset['category'], encode(asset['metadata']), now))
                        normalized_op = {'id': op_id, 'kind': kind, 'client': client, 'asset': asset}
                    else:
                        asset_id = value
                        db.execute('DELETE FROM project_assets WHERE project=? AND asset_id=?',
                                   (identifier, asset_id))
                        normalized_op = {'id': op_id, 'kind': kind, 'client': client, 'assetId': asset_id}
                    current_seq += 1
                    db.execute('INSERT OR REPLACE INTO asset_sequences VALUES(?,?)',
                               (identifier, current_seq))
                    db.execute('''INSERT INTO asset_operations(project,seq,op_id,user,client,payload,created)
                                  VALUES(?,?,?,?,?,?,?)''',
                               (identifier, current_seq, op_id, user, client, encode(normalized_op), now))
                    acked.append(op_id)
                    cutoff = current_seq - ASSET_OPERATION_LIMIT
                    if cutoff > 0:
                        db.execute('DELETE FROM asset_operations WHERE project=? AND seq<=?',
                                   (identifier, cutoff))

                response_ops = []
                reset_assets = []
                asset_reset = False
                if current_seq - cursor > ASSET_RETURN_LIMIT:
                    asset_reset = True
                    reset_assets = [
                        {'id': r['asset_id'], 'path': r['path'], 'sha256': r['hash'], 'size': int(r['size']),
                         'type': r['type'], 'category': r['category'], 'metadata': json.loads(r['metadata'])}
                        for r in db.execute('SELECT * FROM project_assets WHERE project=? ORDER BY path',
                                            (identifier,))
                    ]
                else:
                    rows = db.execute('''SELECT seq,user,client,payload FROM asset_operations
                                         WHERE project=? AND seq>? ORDER BY seq LIMIT ?''',
                                      (identifier, cursor, ASSET_RETURN_LIMIT + 1)).fetchall()
                    if len(rows) > ASSET_RETURN_LIMIT:
                        asset_reset = True
                        reset_assets = [
                            {'id': r['asset_id'], 'path': r['path'], 'sha256': r['hash'], 'size': int(r['size']),
                             'type': r['type'], 'category': r['category'], 'metadata': json.loads(r['metadata'])}
                            for r in db.execute('SELECT * FROM project_assets WHERE project=? ORDER BY path',
                                                (identifier,))
                        ]
                    else:
                        for row in rows:
                            response_ops.append({'seq': row['seq'], 'user': row['user'], 'client': row['client'],
                                                 'operation': json.loads(row['payload'])})
                return {'assetSeq': current_seq, 'acked': acked, 'operations': response_ops,
                        'assetReset': asset_reset, 'resetAssets': reset_assets,
                        'resourceSeq': resource_seq(), 'revision': revision}

            if role == 'viewer':
                raise ApiError(403, 'Seu acesso permite apenas visualizar e baixar.')

            raise ApiError(404, 'Rota não encontrada.')


class Server(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, store, trust_proxy=False):
        self.store = store
        self.trust_proxy = trust_proxy
        self.attempts = {}
        self.rate_lock = threading.Lock()
        # room-watch mantém uma conexão ociosa por usuário para receber mudanças
        # imediatamente. O limite maior evita que watchers ocupem todas as vagas
        # e bloqueiem os pequenos POSTs de edição.
        self.slots = threading.BoundedSemaphore(64)
        self.room_condition = threading.Condition()
        # Geração independente por projeto. Uma edição no Projeto A não deve
        # fazer todos os watchers do Projeto B abrirem uma nova consulta.
        self.room_generations = {}
        self._gc_stop = threading.Event()
        super().__init__(address, Handler)
        self._gc_thread = threading.Thread(target=self._collect_periodically, daemon=True)
        self._gc_thread.start()

    def _collect_periodically(self):
        while not self._gc_stop.wait(3600):
            try:
                self.store.collect_blobs()
            except sqlite3.Error:
                pass  # Retry next cycle; a busy store must not break collaboration.

    def server_close(self):
        self._gc_stop.set()
        super().server_close()
        self._gc_thread.join(timeout=2)

    def room_generation_snapshot(self, project_id):
        with self.room_condition:
            return self.room_generations.get(project_id, 0)

    def notify_room_change(self, project_id):
        with self.room_condition:
            self.room_generations[project_id] = self.room_generations.get(project_id, 0) + 1
            self.room_condition.notify_all()

    def wait_for_room_change(self, project_id, generation, timeout_seconds):
        with self.room_condition:
            return self.room_condition.wait_for(
                lambda: self.room_generations.get(project_id, 0) != generation,
                timeout=timeout_seconds)

    def process_request(self, request, client_address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, client_address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.slots.release()


class Handler(BaseHTTPRequestHandler):
    server_version = 'LudoServer/9'

    def setup(self):
        self.request.settimeout(45)
        super().setup()

    def log_message(self, *args):
        pass  # Nunca registrar senha, token ou payload do projeto.

    def client_ip(self):
        peer = self.client_address[0]
        if self.server.trust_proxy and peer in ('127.0.0.1', '::1'):
            raw = self.headers.get('X-Forwarded-For', '').split(',')[0].strip()
            try:
                return str(ipaddress.ip_address(raw)) if raw else peer
            except ValueError:
                return peer
        return peer

    def do_GET(self):
        self.handle_api('GET')

    def do_POST(self):
        self.handle_api('POST')

    def handle_api(self, method):
        self.connection.settimeout(45)
        try:
            route = urlsplit(self.path).path.strip('/').split('/')
            if not route or route.pop(0) != 'v1':
                raise ApiError(404, 'Rota não encontrada.')
            auth = self.headers.get('Authorization', '')
            token = auth[7:] if auth.startswith('Bearer ') else ''

            # Team Protocol 9: blobs binários viajam fora do JSON. Isso evita
            # o overhead de Base64 e permite cache/deduplicação por SHA-256.
            if (len(route) == 4 and route[0] == 'projects' and route[2] == 'asset-blobs'):
                project_id, blob_hash = route[1], route[3]
                if method == 'GET':
                    payload, size = self.server.store.get_asset_blob(project_id, blob_hash, token)
                    self.respond_bytes(200, payload, 'application/octet-stream',
                                       {'X-LUDO-SHA256': blob_hash, 'X-LUDO-Size': str(size)})
                    return
                if method == 'POST':
                    if self.headers.get('Transfer-Encoding'):
                        raise ApiError(400, 'Envio em blocos não suportado.')
                    try:
                        size = int(self.headers.get('Content-Length', '0'))
                    except ValueError:
                        raise ApiError(400, 'Tamanho inválido.')
                    if size < 0 or size > MAX_ASSET_SIZE:
                        raise ApiError(413, 'O recurso excede o limite de 64 MB.')
                    raw = self.rfile.read(size)
                    if len(raw) != size:
                        raise ApiError(400, 'Envio incompleto.')
                    result = self.server.store.put_asset_blob(project_id, blob_hash, raw, token)
                    self.server.notify_room_change(project_id)
                    self.respond(200, result)
                    return
                raise ApiError(405, 'Método não suportado.')

            if route == ['login']:
                with self.server.rate_lock:
                    now = time.time()
                    ip = self.client_ip()
                    self.server.attempts = {k: v for k, v in self.server.attempts.items()
                                            if now - v[0] < 60}
                    start, count = self.server.attempts.get(ip, (now, 0))
                    self.server.attempts[ip] = (start, count + 1)
                    if count >= 10:
                        raise ApiError(429, 'Aguarde um minuto antes de tentar novamente.')
            body = {}
            if method == 'POST':
                if self.headers.get('Transfer-Encoding'):
                    raise ApiError(400, 'Envio em blocos não suportado.')
                try:
                    size = int(self.headers.get('Content-Length', '0'))
                except ValueError:
                    raise ApiError(400, 'Tamanho inválido.')
                limit = 8192 if route == ['login'] else MAX_BODY
                if size < 0 or size > limit:
                    raise ApiError(413, 'Projeto excede o limite de 128 MB por envio.')
                raw = self.rfile.read(size)
                if len(raw) != size:
                    raise ApiError(400, 'Envio incompleto.')
                try:
                    body = json.loads(raw or b'{}')
                except (ValueError, UnicodeError):
                    raise ApiError(400, 'JSON inválido.')
                if not isinstance(body, dict):
                    raise ApiError(400, 'Requisição inválida.')
            # Team Protocol 9: long-poll de recepção. A espera acontece FORA
            # da transação SQLite; cada consulta room-exchange abre/fecha uma
            # transação curta. Assim um cliente aguardando mudanças não segura
            # o banco nem impede outro usuário de pintar.
            if (method == 'POST' and len(route) == 3 and route[0] == 'projects'
                    and route[2] == 'room-watch'):
                wait_ms = body.get('waitMs', 20000)
                if type(wait_ms) is not int or wait_ms < 250 or wait_ms > 30000:
                    raise ApiError(400, 'Tempo de espera da sala inválido.')
                exchange_body = {'map': body.get('map', ''),
                                 'cursors': body.get('cursors', {}), 'operations': []}
                generation = self.server.room_generation_snapshot(route[1])
                result = self.server.store.dispatch(
                    'POST', ['projects', route[1], 'room-exchange'], exchange_body, token)
                if not result.get('operations') and not result.get('resetMaps'):
                    self.server.wait_for_room_change(route[1], generation, wait_ms / 1000.0)
                    result = self.server.store.dispatch(
                        'POST', ['projects', route[1], 'room-exchange'], exchange_body, token)
                self.respond(200, result)
                return

            result = self.server.store.dispatch(method, route, body, token)
            # Acorde os observadores antes de escrever a resposta do remetente.
            # Assim outro Editor pode começar a receber a mudança imediatamente,
            # em paralelo ao ACK de quem acabou de editar.
            if (method == 'POST' and len(route) >= 3 and route[0] == 'projects'
                    and (route[2] in ('resources-exchange', 'asset-exchange')
                         or (route[2] == 'room-exchange' and bool(body.get('operations'))))):
                changed = route[2] not in ('resources-exchange', 'asset-exchange') or bool(
                    body.get('resources') is not None or body.get('operations'))
                if changed:
                    self.server.notify_room_change(route[1])
            self.respond(200, result)
        except ApiError as error:
            self.respond(error.status, {'error': error.message})
        except (TimeoutError, ConnectionError):
            self.close_connection = True
        except Exception:
            self.respond(500, {'error': 'Não foi possível concluir a operação. Verifique o servidor e o espaço em disco.'})

    def respond_bytes(self, status, data, content_type='application/octet-stream', headers=None):
        try:
            self.send_response(status)
            self.send_header('Content-Type', content_type)
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'private, max-age=31536000, immutable')
            self.send_header('X-Content-Type-Options', 'nosniff')
            for key, value in (headers or {}).items():
                self.send_header(key, value)
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.write(data)
        except (OSError, ConnectionError):
            pass
        self.close_connection = True

    def respond(self, status, value):
        data = encode(value)
        try:
            self.send_response(status)
            self.send_header('Content-Type', 'application/json; charset=utf-8')
            self.send_header('Content-Length', str(len(data)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Referrer-Policy', 'no-referrer')
            self.send_header('Connection', 'close')
            self.end_headers()
            self.wfile.write(data)
        except (OSError, ConnectionError):
            pass
        self.close_connection = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--data', default=str(Path.home() / 'LudoServidor'))
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8787)
    parser.add_argument('--name', help='Nome exibido aos clientes.')
    parser.add_argument('--public-url', default=None,
                        help='URL HTTPS pública informativa, ex.: https://equipe.exemplo.com')
    parser.add_argument('--trust-proxy', action='store_true',
                        help='Confia em X-Forwarded-For apenas quando o proxy conecta por loopback.')
    parser.add_argument('--stop-file', default=None,
                        help='Arquivo-sinal local usado pelo LUDO Editor para encerrar o servidor.')
    parser.add_argument('--add-user')
    parser.add_argument('--remove-user')
    parser.add_argument('--list-users-json', action='store_true')
    parser.add_argument('--password-stdin', action='store_true',
                        help='Lê a senha do novo usuário pela entrada padrão. Usado pelo Editor.')
    parser.add_argument('--role', choices=['admin', 'editor', 'viewer'], default='editor')
    parser.add_argument('--backup', action='store_true')
    args = parser.parse_args()
    store = Store(args.data, args.name, args.public_url)
    if args.list_users_json:
        print(json.dumps(store.users(), ensure_ascii=False, separators=(',', ':')))
        return
    if args.remove_user:
        store.remove_user(args.remove_user)
        print('Usuário removido.')
        return
    if args.add_user:
        if args.password_stdin:
            import sys
            password = sys.stdin.readline().rstrip('\r\n')
        else:
            import getpass
            password = getpass.getpass('Senha (mínimo 10 caracteres): ')
        store.add_user(args.add_user, password, args.role)
        print('Usuário criado.')
        return
    if args.password_stdin:
        parser.error('--password-stdin só pode ser usado junto de --add-user.')
    if args.backup:
        print(store.backup())
        return
    if not store.users():
        parser.error('Crie o administrador com --add-user NOME --role admin.')
    server = Server((args.host, args.port), store, trust_proxy=args.trust_proxy)
    info = store.info()
    display = info['publicUrl'] or f'http://{args.host}:{server.server_port}'
    print(f"{info['serverName']}: {display} — API {PROTOCOL}, sincronização {TEAM_PROTOCOL}")
    if args.host not in ('127.0.0.1', '::1') and not info['publicUrl']:
        print('AVISO: para Internet, exponha somente um proxy HTTPS; não publique a porta interna diretamente.')

    stop_path = Path(args.stop_file) if args.stop_file else None
    stop_watch = threading.Event()

    # Descoberta local do Hub de Equipe. O servidor apenas anuncia metadados
    # públicos (nome, porta e versão); credenciais e projetos continuam
    # protegidos pela API HTTP. Broadcast é limitado à rede local e não
    # substitui servidores salvos para VPN/Internet.
    if args.host not in ('127.0.0.1', '::1'):
        def broadcast_discovery():
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            try:
                sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
                while not stop_watch.wait(1.5):
                    try:
                        payload = encode(store.discovery_info(server.server_port))
                        sock.sendto(payload, ('255.255.255.255', 47877))
                    except (OSError, sqlite3.Error):
                        pass
            finally:
                sock.close()
        threading.Thread(target=broadcast_discovery, name='ludo-discovery', daemon=True).start()
    if stop_path:
        try:
            stop_path.unlink(missing_ok=True)
        except OSError:
            pass

        def watch_stop_file():
            while not stop_watch.wait(0.25):
                if not stop_path.exists():
                    continue
                try:
                    stop_path.unlink(missing_ok=True)
                except OSError:
                    pass
                server.shutdown()
                return

        threading.Thread(target=watch_stop_file, name='ludo-stop-watch', daemon=True).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        stop_watch.set()
        if stop_path:
            try:
                stop_path.unlink(missing_ok=True)
            except OSError:
                pass
        server.server_close()


if __name__ == '__main__':
    main()
