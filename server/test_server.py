import copy
import json
import hashlib
import subprocess
import socket
import sys
import tempfile
import threading
import time
import unittest
import urllib.request
import urllib.error
from pathlib import Path
import ludo_server
from ludo_server import Store, Server, clean_payload

# Keep production at 250k PBKDF2 rounds; tests use a tiny value so CTest stays fast.
ludo_server.PASSWORD_ROUNDS = 1000


def project():
    def layers():
        return [
            {'id':'ground','type':'tilelayer','data2D':[[None for _x in range(10)] for _y in range(10)]},
            {'id':'objects','type':'objectgroup','objects':[]}
        ]
    return {'format':'LudoMapProject','formatVersion':3,'projectId':'local-project','projectName':'Teste',
            'tilesets':[{'id':'tiles','src':'data:image/png;base64,AA=='}],
            'maps':[{'id':name,'name':name,'map':{'width':10,'height':10},'layers':layers()} for name in ['rua','casa']]}

class Integration(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.store=Store(self.temp.name)
        for name,role in [('admin','admin'),('alice','editor'),('bob','editor'),('reader','viewer')]:self.store.add_user(name,'senha-de-teste',role)
        self.server=Server(('127.0.0.1',0),self.store)
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
        self.url='http://127.0.0.1:'+str(self.server.server_port)+'/v1/'
        self.tokens={}
        for name in ['admin','alice','bob','reader']:
            self.tokens[name]=self.call('login',{'name':name,'password':'senha-de-teste'})[1]['token']
        self.initial=project()
        status,response=self.call('projects',{'payload':self.initial},'admin');self.assertEqual(status,200)
        self.pid=response['id'];self.base='projects/'+self.pid
    def tearDown(self):
        self.server.shutdown();self.server.server_close();self.thread.join();self.temp.cleanup()
    def call(self,path,body=None,user=None):
        headers={'Content-Type':'application/json'}
        if user:headers['Authorization']='Bearer '+self.tokens[user]
        req=urllib.request.Request(self.url+path,data=None if body is None else json.dumps(body).encode(),headers=headers)
        try:
            with urllib.request.urlopen(req,timeout=10) as response:return response.status,json.load(response)
        except urllib.error.HTTPError as error:return error.code,json.load(error)
    def lease(self,user,map_id):return self.call(self.base+'/lease',{'map':map_id},user)
    def commit(self,user,payload,revision=1):return self.call(self.base+'/commit',{'revision':revision,'payload':payload},user)
    def room(self,user,operations=None,cursor=0,map_id='rua'):
        return self.call(self.base+'/room-exchange',{'map':map_id,'cursors':{map_id:cursor},'operations':operations or []},user)
    def call_blob(self,blob_hash,payload=None,user='alice'):
        headers={'Authorization':'Bearer '+self.tokens[user]}
        url=self.url+self.base+'/asset-blobs/'+blob_hash
        req=urllib.request.Request(url,data=payload,headers=headers,method='POST' if payload is not None else 'GET')
        if payload is not None:headers['Content-Type']='application/octet-stream'
        try:
            with urllib.request.urlopen(req,timeout=10) as response:return response.status,response.read()
        except urllib.error.HTTPError as error:return error.code,error.read()
    def patch_resources(self, user, op_id, operations, base=0):
        return self.call(self.base+'/resources-exchange', {'cursor':0,'baseSeq':base,'id':op_id,'operations':operations}, user)

    def test_protocol9_map_properties_preserve_layers_and_reject_resize(self):
        props=copy.deepcopy(self.initial['maps'][0]['map']);props['background']='#102030'
        status,_=self.room('alice',[{'id':'props','map':'rua','kind':'map.properties','value':{'map':props}}])
        self.assertEqual(status,200)
        payload=self.call(self.base,user='bob')[1]['payload']['maps'][0]
        self.assertEqual(payload['map']['background'],'#102030')
        self.assertEqual(payload['layers'],self.initial['maps'][0]['layers'])
        props['width']=99
        status,_=self.room('bob',[{'id':'resize','map':'rua','kind':'map.properties','value':{'map':props}}])
        self.assertEqual(status,409)
        status,response=self.room('alice');self.assertEqual(response['roomSeqs']['rua'],1)

    def test_protocol9_map_property_fields_do_not_overwrite_each_other(self):
        for user,field,value in [('alice','background','#112233'),('bob','depthEnabled',True)]:
            status,_=self.room(user,[{'id':field,'map':'rua','kind':'map.properties','value':{'map':{field:value}}}])
            self.assertEqual(status,200)
        props=self.call(self.base,user='reader')[1]['payload']['maps'][0]['map']
        self.assertEqual(props['background'],'#112233');self.assertTrue(props['depthEnabled']);self.assertEqual(props['width'],10)

    def test_protocol9_resource_distinct_fields_merge_and_retry(self):
        a={'kind':'resource.set','path':['projectName'],'beforeExists':True,'before':'Teste','value':'Novo'}
        b={'kind':'resource.set','path':['randomPool'],'beforeExists':False,'value':[{'tile':1}]}
        self.assertEqual(self.patch_resources('alice','a',[a])[0],200)
        status,result=self.patch_resources('bob','b',[b]);self.assertEqual(status,200);self.assertFalse(result['conflict'])
        self.assertEqual(result['resources']['projectName'],'Novo');self.assertIn('randomPool',result['resources'])
        status,retry=self.patch_resources('bob','b',[b]);self.assertEqual(retry['resourceSeq'],2)
        self.assertEqual(self.patch_resources('reader','viewer',[a])[0],403)

    def test_protocol9_resource_same_field_conflict_is_atomic(self):
        a={'kind':'resource.set','path':['projectName'],'beforeExists':True,'before':'Teste','value':'Alice'}
        self.patch_resources('alice','a',[a])
        b=dict(a,value='Bob')
        new={'kind':'resource.set','path':['randomPool'],'beforeExists':False,'value':[]}
        status,result=self.patch_resources('bob','b',[new,b]);self.assertEqual(status,200);self.assertTrue(result['conflict'])
        self.assertNotIn('randomPool',result['resources']);self.assertEqual(result['resources']['projectName'],'Alice')

    def test_protocol9_resource_items_have_independent_conflicts(self):
        add={'kind':'resource.set','path':['tilesets',{'id':'second'}],'beforeExists':False,'value':{'id':'second','name':'B'}}
        self.assertFalse(self.patch_resources('alice','add',[add])[1]['conflict'])
        a={'kind':'resource.set','path':['tilesets',{'id':'tiles'},'name'],'beforeExists':False,'value':'A'}
        b={'kind':'resource.set','path':['tilesets',{'id':'second'},'name'],'beforeExists':True,'before':'B','value':'B2'}
        self.assertFalse(self.patch_resources('alice','a',[a])[1]['conflict'])
        response=self.patch_resources('bob','b',[b])[1];self.assertFalse(response['conflict'])
        self.assertEqual([v['name'] for v in response['resources']['tilesets']],['A','B2'])
        delete={'kind':'resource.delete','path':['tilesets',{'id':'second'}],'beforeExists':True,'before':{'id':'second','name':'B2'}}
        self.assertEqual(len(self.patch_resources('bob','delete',[delete])[1]['resources']['tilesets']),1)

    def test_protocol9_resource_protected_fields_and_invalid_base(self):
        for path in (['maps'],['projectId'],['format'],['tilesets',{'bad':'id'}]):
            status,_=self.patch_resources('alice',str(path),[{'kind':'resource.set','path':path,'beforeExists':False,'value':[]}])
            self.assertEqual(status,400)
        status,_=self.patch_resources('alice','invalid',[{'kind':'resource.delete','path':['projectName']}]);self.assertEqual(status,400)

    def test_protocol9_raster_blob_roundtrip_and_checkpoint(self):
        import base64
        png=base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=')
        digest=hashlib.sha256(png).hexdigest()
        self.assertEqual(self.call_blob(digest,png)[0],200)
        layer={'id':'ground','type':'imagelayer','imageSrc':{'$raster':digest}}
        operation={'id':'raster','map':'rua','kind':'layer.replace','layer':'ground','value':layer}
        status,result=self.room('alice',[operation]);self.assertEqual(status,200)
        self.assertEqual(self.call_blob(digest,None,'bob')[1],png)
        status,remote=self.room('bob');self.assertEqual(status,200)
        self.assertEqual(remote['operations'][0]['operation']['value']['imageSrc'],{'$raster':digest})
        payload=self.call(self.base,user='bob')[1]['payload']
        self.assertEqual(payload['maps'][0]['layers'][0]['imageSrc'],'data:image/png;base64,'+base64.b64encode(png).decode())
        self.assertEqual(self.room('alice',[operation])[1]['roomSeqs']['rua'],1)
        self.assertEqual(self.store.collect_blobs(now=time.time()+172800),0)

    def test_protocol9_raster_missing_upload_is_atomic(self):
        operation={'id':'raster-missing','map':'rua','kind':'layer.replace','layer':'ground','value':{'id':'ground','type':'imagelayer','imageSrc':{'$raster':'0'*64}}}
        self.assertEqual(self.room('alice',[operation])[0],409)
        self.assertEqual(self.room('bob')[1]['roomSeqs']['rua'],0)

    def test_protocol9_blob_gc_preserves_recent_and_manifest(self):
        old=b'old';recent=b'recent';active=b'active'
        hashes=[hashlib.sha256(v).hexdigest() for v in (old,recent,active)]
        for digest,data in zip(hashes,(old,recent,active)):self.assertEqual(self.call_blob(digest,data)[0],200)
        asset={'id':'active','path':'Assets/audio.bin','sha256':hashes[2],'size':len(active),'type':'audio'}
        self.assertEqual(self.call(self.base+'/asset-exchange',{'cursor':0,'operations':[{'id':'add-active','kind':'asset.upsert','asset':asset}]},'alice')[0],200)
        with self.store.connect() as db:
            db.execute('UPDATE asset_blobs SET created=?',(time.time()-172800,))
            db.execute('UPDATE project_blob_uploads SET created=? WHERE hash!=?',(time.time()-172800,hashes[1]))
        self.assertEqual(self.store.collect_blobs(),1)
        self.assertEqual(self.call_blob(hashes[2],None,'bob')[1],active)
        status,response=self.call(self.base+'/asset-blobs-check',{'hashes':hashes},'alice')
        self.assertEqual(response['missing'],[hashes[0]])

    def test_authentication_and_roles(self):
        self.assertEqual(self.call('projects')[0],401)
        self.assertEqual(self.call('projects',{'payload':project()},'alice')[0],403)
        self.assertEqual(self.lease('reader','rua')[0],403)
        self.assertEqual(self.call(self.base,user='reader')[0],200)
        self.assertEqual(self.call('login',{'name':'alice','password':'wrong'})[0],401)
    def test_normalization_does_not_share_host_paths(self):
        payload=project();payload['rpgMakerProjectRoot']='C:/private/game';payload['activeMapDocIdx']=1
        payload['tilesets'][0]['source']='C:/private/image.png';payload['maps'][0]['activeLayerId']='x'
        cleaned=clean_payload(payload)
        self.assertNotIn('rpgMakerProjectRoot',cleaned)
        self.assertNotIn('source',cleaned['tilesets'][0]);self.assertNotIn('activeLayerId',cleaned['maps'][0])
    def test_user_removal_revokes_access_and_protects_last_admin(self):
        self.store.remove_user('alice')
        self.assertEqual(self.call('projects',user='alice')[0],401)
        self.assertEqual(self.call(self.base+'/room-exchange',{'map':'rua','cursors':{'rua':0},'operations':[]},'bob')[0],200)
        with self.assertRaises(ValueError):self.store.remove_user('admin')
    def test_protocol9_health_and_stable_server_identity(self):
        status,health=self.call('health')
        self.assertEqual(status,200);self.assertEqual(health['protocol'],1);self.assertEqual(health['teamProtocol'],9)
        self.assertTrue(health['serverId']);self.assertEqual(health['mode'],'cloud-ready')
        self.assertEqual(Store(self.temp.name).info()['serverId'],health['serverId'])

    def test_hub_discovery_exposes_only_public_summary(self):
        info=self.store.discovery_info(8787)
        self.assertEqual(info['type'],'ludo-team');self.assertEqual(info['discoveryVersion'],1)
        self.assertEqual(info['teamProtocol'],9);self.assertEqual(info['port'],8787)
        self.assertIn('serverId',info);self.assertIn('serverName',info)
        self.assertEqual(info['projects'],1);self.assertGreaterEqual(info['online'],0)
        for secret in ('token','password','payload'):
            self.assertNotIn(secret,info)

    def test_project_list_exposes_hub_summary_without_payload(self):
        self.call(self.base+'/heartbeat',{'map':'rua'},'alice')
        status,result=self.call('projects',user='admin')
        self.assertEqual(status,200);self.assertEqual(len(result['projects']),1)
        item=result['projects'][0]
        for key in ('id','name','revision','roomSeqTotal','resourceSeq','assetSeq','online','updated'):
            self.assertIn(key,item)
        self.assertNotIn('payload',item);self.assertGreaterEqual(item['online'],1)



    def test_presence_tracks_active_map_for_all_roles(self):
        for user,map_id in [('alice','rua'),('bob','casa'),('reader','rua')]:
            status,result=self.call(self.base+'/heartbeat',{'map':map_id},user)
            self.assertEqual(status,200)
        presence=self.call(self.base+'/status',user='admin')[1]['presence']
        found={(item['user'],item['map']) for item in presence}
        self.assertIn(('alice','rua'),found);self.assertIn(('bob','casa'),found);self.assertIn(('reader','rua'),found)

    def test_room_merges_tile_cells_and_does_not_create_versions(self):
        alice=[{'id':'alice-1','client':'alice-client','map':'rua','kind':'tile.cells','layer':'ground',
                'changes':[{'x':1,'y':2,'value':{'tiles':['A']}}]}]
        bob=[{'id':'bob-1','client':'bob-client','map':'rua','kind':'tile.cells','layer':'ground',
              'changes':[{'x':7,'y':4,'value':{'tiles':['B']}}]}]
        status,a=self.room('alice',alice);self.assertEqual(status,200);self.assertEqual(a['roomSeqs']['rua'],1)
        status,b=self.room('bob',bob);self.assertEqual(status,200);self.assertEqual(b['roomSeqs']['rua'],2)
        current=self.call(self.base,user='reader')[1]
        ground=current['payload']['maps'][0]['layers'][0]['data2D']
        self.assertEqual(ground[2][1],{'tiles':['A']});self.assertEqual(ground[4][7],{'tiles':['B']})
        self.assertEqual(current['revision'],1)
        with self.store.connect() as db:
            self.assertEqual(db.execute('SELECT count(*) FROM versions WHERE project=?',(self.pid,)).fetchone()[0],1)
            checkpoint=json.loads(db.execute('SELECT payload FROM live_projects WHERE project=?',(self.pid,)).fetchone()['payload'])
            self.assertIsNone(checkpoint['maps'][0]['layers'][0]['data2D'][2][1])

    def test_room_same_cell_uses_server_sequence_order(self):
        first=[{'id':'a-cell','client':'a','map':'rua','kind':'tile.cells','layer':'ground',
                'changes':[{'x':3,'y':3,'value':{'tiles':['A']}}]}]
        second=[{'id':'b-cell','client':'b','map':'rua','kind':'tile.cells','layer':'ground',
                 'changes':[{'x':3,'y':3,'value':{'tiles':['B']}}]}]
        self.assertEqual(self.room('alice',first)[0],200);self.assertEqual(self.room('bob',second)[0],200)
        cell=self.call(self.base,user='reader')[1]['payload']['maps'][0]['layers'][0]['data2D'][3][3]
        self.assertEqual(cell,{'tiles':['B']})

    def test_room_operation_retry_is_idempotent(self):
        op=[{'id':'same-op','client':'retry-client','map':'rua','kind':'region.cells',
             'changes':[{'x':2,'y':2,'value':9}]}]
        first=self.room('alice',op);second=self.room('alice',op)
        self.assertEqual(first[0],200);self.assertEqual(second[0],200)
        self.assertEqual(second[1]['roomSeqs']['rua'],1)
        with self.store.connect() as db:self.assertEqual(db.execute('SELECT count(*) FROM room_operations WHERE project=?',(self.pid,)).fetchone()[0],1)

    def test_room_object_patches_merge_by_object_id(self):
        a=[{'id':'obj-a','client':'a','map':'rua','kind':'objects.patch','layer':'objects',
            'upsert':[{'id':'chair-a','name':'A','x':1,'y':1}],'delete':[]}]
        b=[{'id':'obj-b','client':'b','map':'rua','kind':'objects.patch','layer':'objects',
            'upsert':[{'id':'chair-b','name':'B','x':8,'y':8}],'delete':[]}]
        self.assertEqual(self.room('alice',a)[0],200);self.assertEqual(self.room('bob',b)[0],200)
        objects=self.call(self.base,user='reader')[1]['payload']['maps'][0]['layers'][1]['objects']
        self.assertEqual({o['id'] for o in objects},{'chair-a','chair-b'})

    def test_room_map_replace_rejects_stale_structural_base(self):
        replacement=copy.deepcopy(self.initial['maps'][0]);replacement['name']='Alice estrutura'
        a=[{'id':'map-a','client':'a','map':'rua','kind':'map.replace','baseSeq':0,'value':replacement}]
        self.assertEqual(self.room('alice',a)[0],200)
        stale=copy.deepcopy(self.initial['maps'][0]);stale['name']='Bob estrutura antiga'
        b=[{'id':'map-b','client':'b','map':'rua','kind':'map.replace','baseSeq':0,'value':stale}]
        self.assertEqual(self.room('bob',b)[0],409)
        self.assertEqual(self.call(self.base,user='reader')[1]['payload']['maps'][0]['name'],'Alice estrutura')

    def test_room_periodic_checkpoint_preserves_history_without_version_per_tick(self):
        old_limit=ludo_server.ROOM_CHECKPOINT_OPERATIONS
        old_seconds=ludo_server.ROOM_CHECKPOINT_SECONDS
        ludo_server.ROOM_CHECKPOINT_OPERATIONS=2;ludo_server.ROOM_CHECKPOINT_SECONDS=999999
        try:
            a=[{'id':'cp-a','client':'a','map':'rua','kind':'region.cells','changes':[{'x':4,'y':4,'value':1}]}]
            b=[{'id':'cp-b','client':'b','map':'rua','kind':'region.cells','changes':[{'x':5,'y':5,'value':2}]}]
            self.assertEqual(self.room('alice',a)[1]['revision'],1)
            second=self.room('bob',b)[1];self.assertEqual(second['revision'],2)
            versions=self.call(self.base+'/history',user='reader')[1]['versions']
            self.assertEqual([v['revision'] for v in versions],[2,1])
            checkpoint=self.call(self.base+'/versions/2',user='reader')[1]['payload']
            self.assertEqual(checkpoint['maps'][0]['rpgMakerRegions'],{'4,4':1,'5,5':2})
        finally:
            ludo_server.ROOM_CHECKPOINT_OPERATIONS=old_limit
            ludo_server.ROOM_CHECKPOINT_SECONDS=old_seconds

    def test_room_presence_and_viewer_receive(self):
        op=[{'id':'presence-a','client':'a','map':'rua','kind':'region.cells','changes':[{'x':1,'y':1,'value':4}]}]
        self.assertEqual(self.room('alice',op)[0],200)
        status,result=self.room('reader',[],0,'rua');self.assertEqual(status,200)
        self.assertTrue(any(item['user']=='alice' and item['map']=='rua' for item in result['presence']))
        self.assertTrue(any(item['operation']['id']=='presence-a' for item in result['operations']))
        forbidden=[{'id':'reader-write','client':'reader','map':'rua','kind':'region.cells','changes':[{'x':0,'y':0,'value':1}]}]
        self.assertEqual(self.room('reader',forbidden)[0],403)

    def test_room_watch_wakes_immediately_when_another_user_edits(self):
        result={}
        started=threading.Event()
        def watch():
            started.set()
            begin=time.monotonic()
            status,payload=self.call(self.base+'/room-watch',
                {'map':'rua','cursors':{'rua':0},'waitMs':3000},'bob')
            result['status']=status;result['payload']=payload;result['elapsed']=time.monotonic()-begin
        thread=threading.Thread(target=watch);thread.start();started.wait(1);time.sleep(.15)
        op=[{'id':'live-now','client':'alice-live','map':'rua','kind':'region.cells',
             'changes':[{'x':6,'y':6,'value':7}]}]
        self.assertEqual(self.room('alice',op)[0],200)
        thread.join(2)
        self.assertFalse(thread.is_alive())
        self.assertEqual(result['status'],200)
        self.assertLess(result['elapsed'],1.5)
        self.assertTrue(any(item['operation']['id']=='live-now' for item in result['payload']['operations']))

    def test_unknown_room_cursor_is_ignored_while_new_local_map_is_being_created(self):
        status,result=self.call(self.base+'/room-exchange',
            {'map':'novo-local','cursors':{'novo-local':0},'operations':[]},'alice')
        self.assertEqual(status,200)
        self.assertNotIn('novo-local',result['roomSeqs'])

    def test_map_create_enters_room_without_reservation_and_creates_checkpoint(self):
        new_map={'id':'novo','name':'Novo','map':{'width':10,'height':10},
                 'layers':[{'id':'ground-new','type':'tilelayer','data2D':[[None for _x in range(10)] for _y in range(10)]}]}
        op={'id':'create-new-map','client':'alice-client','map':'novo','kind':'map.create','value':new_map}
        status,result=self.call(self.base+'/room-exchange',
            {'map':'novo','cursors':{'novo':0},'operations':[op]},'alice')
        self.assertEqual(status,200);self.assertEqual(result['roomSeqs']['novo'],1)
        self.assertGreaterEqual(result['revision'],2)
        current=self.call(self.base,user='reader')[1]
        self.assertIn('novo',[item['id'] for item in current['payload']['maps']])
        self.assertEqual(current['payload']['maps'][-1]['name'],'Novo')
        # Reenvio da mesma operação continua idempotente.
        status,retry=self.call(self.base+'/room-exchange',
            {'map':'novo','cursors':{'novo':0},'operations':[op]},'alice')
        self.assertEqual(status,200);self.assertEqual(retry['roomSeqs']['novo'],1)

    def test_protocol9_map_metadata_is_a_project_operation(self):
        op={'id':'meta-casa-1','client':'alice-client','map':'casa','kind':'map.meta',
            'value':{'name':'Casa nova','parentId':'rua','variationBaseId':'rua',
                     'variationName':'Noite','rpgMakerMapId':12,'rpgMakerImported':True}}
        status,result=self.call(self.base+'/room-exchange',
            {'map':'rua','cursors':{'rua':0,'casa':0},'operations':[op]},'alice')
        self.assertEqual(status,200);self.assertIn('meta-casa-1',result['acked'])
        self.assertGreaterEqual(result['revision'],2);self.assertEqual(result['roomSeqs']['casa'],1)
        maps={m['id']:m for m in self.call(self.base,user='reader')[1]['payload']['maps']}
        casa=maps['casa'];self.assertEqual(casa['name'],'Casa nova');self.assertEqual(casa['parentId'],'rua')
        self.assertEqual(casa['variationBaseId'],'rua');self.assertEqual(casa['variationName'],'Noite')
        self.assertEqual(casa['rpgMakerMapId'],12);self.assertTrue(casa['rpgMakerImported'])

    def test_protocol9_inactive_room_observes_metadata_through_revision_delta(self):
        result={};started=threading.Event()
        def watch():
            started.set();status,payload=self.call(self.base+'/room-watch',
                {'map':'rua','cursors':{'rua':0},'waitMs':3000},'bob')
            result['status']=status;result['payload']=payload
        thread=threading.Thread(target=watch);thread.start();started.wait(1);time.sleep(.12)
        op={'id':'meta-inactive-casa','client':'alice-client','map':'casa','kind':'map.meta',
            'value':{'name':'Casa remota','parentId':'rua','variationBaseId':'',
                     'variationName':'','rpgMakerMapId':4,'rpgMakerImported':False}}
        self.assertEqual(self.call(self.base+'/room-exchange',
            {'map':'casa','cursors':{'casa':0},'operations':[op]},'alice')[0],200)
        thread.join(2);self.assertFalse(thread.is_alive());self.assertEqual(result['status'],200)
        self.assertGreaterEqual(result['payload']['revision'],2)
        # O cliente que está em outra sala recebe a estrutura pelo delta do
        # checkpoint, sem precisar abrir o mapa alterado primeiro.
        status,delta=self.call(self.base+'/sync',{'revision':1},'bob')
        self.assertEqual(status,200);changed={m['id']:m for m in delta['delta']['maps']}
        self.assertEqual(changed['casa']['name'],'Casa remota');self.assertEqual(changed['casa']['parentId'],'rua')
        self.assertEqual(changed['casa']['rpgMakerMapId'],4)

    def test_protocol9_map_order_is_explicit_and_persisted(self):
        op={'id':'order-1','client':'alice-client','map':'rua','kind':'map.order','order':['casa','rua']}
        status,result=self.call(self.base+'/room-exchange',
            {'map':'rua','cursors':{'rua':0},'operations':[op]},'alice')
        self.assertEqual(status,200);self.assertIn('order-1',result['acked']);self.assertGreaterEqual(result['revision'],2)
        current=self.call(self.base,user='reader')[1]['payload']
        self.assertEqual([m['id'] for m in current['maps']],['casa','rua'])

    def test_protocol9_order_is_exposed_by_structural_delta(self):
        op={'id':'order-delta','client':'alice-client','map':'rua','kind':'map.order','order':['casa','rua']}
        self.assertEqual(self.call(self.base+'/room-exchange',
            {'map':'rua','cursors':{'rua':0},'operations':[op]},'alice')[0],200)
        status,result=self.call(self.base+'/sync',{'revision':1},'bob')
        self.assertEqual(status,200);self.assertEqual(result['delta']['mapOrder'],['casa','rua'])

    def test_protocol9_map_delete_is_idempotent_and_does_not_leave_ghost(self):
        op={'id':'delete-rua-1','client':'alice-client','map':'rua','kind':'map.delete'}
        status,result=self.call(self.base+'/room-exchange',
            {'map':'casa','cursors':{'rua':0,'casa':0},'operations':[op]},'alice')
        self.assertEqual(status,200);self.assertIn('delete-rua-1',result['acked']);self.assertNotIn('rua',result['roomSeqs'])
        self.assertEqual([m['id'] for m in self.call(self.base,user='reader')[1]['payload']['maps']],['casa'])
        # O checkpoint da exclusão remove o log da sala antiga. Mesmo assim,
        # repetir exatamente a operação continua sendo um ACK idempotente.
        status,retry=self.call(self.base+'/room-exchange',
            {'map':'casa','cursors':{'casa':0},'operations':[op]},'alice')
        self.assertEqual(status,200);self.assertIn('delete-rua-1',retry['acked'])
        self.assertEqual([m['id'] for m in self.call(self.base,user='reader')[1]['payload']['maps']],['casa'])

    def test_protocol9_refuses_to_delete_final_map_atomically(self):
        first={'id':'delete-rua-first','client':'alice-client','map':'rua','kind':'map.delete'}
        self.assertEqual(self.call(self.base+'/room-exchange',
            {'map':'casa','cursors':{'casa':0},'operations':[first]},'alice')[0],200)
        final={'id':'delete-casa-final','client':'alice-client','map':'casa','kind':'map.delete'}
        status,_=self.call(self.base+'/room-exchange',
            {'map':'casa','cursors':{'casa':0},'operations':[final]},'alice')
        self.assertEqual(status,409)
        self.assertEqual([m['id'] for m in self.call(self.base,user='reader')[1]['payload']['maps']],['casa'])

    def test_room_operation_validation_is_atomic_before_sequence_advance(self):
        op={'id':'invalid-partial-tile','client':'alice-client','map':'rua','kind':'tile.cells','layer':'ground',
            'changes':[{'x':1,'y':1,'value':{'tiles':['OK']}},
                       {'x':999,'y':1,'value':{'tiles':['INVALID']}}]}
        status,_=self.call(self.base+'/room-exchange',
            {'map':'rua','cursors':{'rua':0},'operations':[op]},'alice')
        self.assertEqual(status,400)
        current=self.call(self.base,user='reader')[1]
        rua=next(m for m in current['payload']['maps'] if m['id']=='rua')
        ground=next(layer for layer in rua['layers'] if layer['id']=='ground')
        self.assertIsNone(ground['data2D'][1][1])
        self.assertNotIn('rua',current['roomSeqs'])

    def test_protocol9_forced_room_reset_returns_authoritative_snapshot(self):
        op={'id':'tile-before-reset','client':'alice-client','map':'rua','kind':'tile.cells','layer':'ground',
            'changes':[{'x':2,'y':3,'value':{'tiles':['A']}}]}
        self.assertEqual(self.room('alice',[op])[0],200)
        status,result=self.call(self.base+'/room-exchange',
            {'map':'rua','cursors':{'rua':0},'operations':[],'resetMaps':['rua']},'bob')
        self.assertEqual(status,200);self.assertEqual(result['operations'],[]);self.assertEqual(result['roomSeqs']['rua'],1)
        self.assertEqual(len(result['resetMaps']),1);reset=result['resetMaps'][0]
        ground=next(layer for layer in reset['layers'] if layer['id']=='ground')
        self.assertEqual(ground['data2D'][3][2],{'tiles':['A']})

    def test_protocol9_create_then_order_can_share_one_batch(self):
        new_map={'id':'novo-ordenado','name':'Novo ordenado','map':{'width':10,'height':10},'layers':[]}
        operations=[
            {'id':'create-ordered','client':'alice-client','map':'novo-ordenado','kind':'map.create','value':new_map},
            {'id':'order-after-create','client':'alice-client','map':'rua','kind':'map.order',
             'order':['novo-ordenado','rua','casa']}
        ]
        status,result=self.call(self.base+'/room-exchange',
            {'map':'rua','cursors':{'rua':0},'operations':operations},'alice')
        self.assertEqual(status,200);self.assertEqual(set(result['acked']),{'create-ordered','order-after-create'})
        self.assertEqual([m['id'] for m in self.call(self.base,user='reader')[1]['payload']['maps']],
                         ['novo-ordenado','rua','casa'])

    def test_protocol9_invalid_metadata_reference_is_atomic(self):
        op={'id':'bad-parent','client':'alice-client','map':'casa','kind':'map.meta',
            'value':{'name':'Não deve entrar','parentId':'inexistente','variationBaseId':'',
                     'variationName':'','rpgMakerMapId':0,'rpgMakerImported':False}}
        status,_=self.call(self.base+'/room-exchange',
            {'map':'casa','cursors':{'casa':0},'operations':[op]},'alice')
        self.assertEqual(status,409)
        maps={m['id']:m for m in self.call(self.base,user='reader')[1]['payload']['maps']}
        self.assertEqual(maps['casa']['name'],'casa');self.assertNotIn('parentId',maps['casa'])
        self.assertNotIn('casa',self.call(self.base,user='reader')[1]['roomSeqs'])

    def test_protocol9_delete_cannot_leave_dangling_parent_reference(self):
        parent={'id':'parent-casa','client':'alice-client','map':'casa','kind':'map.meta',
            'value':{'name':'casa','parentId':'rua','variationBaseId':'',
                     'variationName':'','rpgMakerMapId':0,'rpgMakerImported':False}}
        self.assertEqual(self.call(self.base+'/room-exchange',
            {'map':'casa','cursors':{'casa':0},'operations':[parent]},'alice')[0],200)
        delete={'id':'delete-parent-with-child','client':'alice-client','map':'rua','kind':'map.delete'}
        status,_=self.call(self.base+'/room-exchange',
            {'map':'casa','cursors':{'casa':1},'operations':[delete]},'alice')
        self.assertEqual(status,409)
        maps={m['id']:m for m in self.call(self.base,user='reader')[1]['payload']['maps']}
        self.assertIn('rua',maps);self.assertEqual(maps['casa']['parentId'],'rua')

    def test_protocol9_rejects_parent_cycle_atomically(self):
        operations=[
            {'id':'cycle-a','client':'alice-client','map':'rua','kind':'map.meta',
             'value':{'name':'rua','parentId':'casa','variationBaseId':'',
                      'variationName':'','rpgMakerMapId':0,'rpgMakerImported':False}},
            {'id':'cycle-b','client':'alice-client','map':'casa','kind':'map.meta',
             'value':{'name':'casa','parentId':'rua','variationBaseId':'',
                      'variationName':'','rpgMakerMapId':0,'rpgMakerImported':False}}
        ]
        status,_=self.call(self.base+'/room-exchange',
            {'map':'rua','cursors':{'rua':0,'casa':0},'operations':operations},'alice')
        self.assertEqual(status,409)
        maps={m['id']:m for m in self.call(self.base,user='reader')[1]['payload']['maps']}
        self.assertNotIn('parentId',maps['rua']);self.assertNotIn('parentId',maps['casa'])
        self.assertNotIn('rua',self.call(self.base,user='reader')[1]['roomSeqs'])
        self.assertNotIn('casa',self.call(self.base,user='reader')[1]['roomSeqs'])

    def test_map_create_wakes_existing_room_watch_with_new_revision(self):
        result={};started=threading.Event()
        def watch():
            started.set();begin=time.monotonic()
            status,payload=self.call(self.base+'/room-watch',
                {'map':'rua','cursors':{'rua':0},'waitMs':3000},'bob')
            result['status']=status;result['payload']=payload;result['elapsed']=time.monotonic()-begin
        thread=threading.Thread(target=watch);thread.start();started.wait(1);time.sleep(.12)
        new_map={'id':'sala-nova','name':'Sala nova','map':{'width':10,'height':10},'layers':[]}
        op={'id':'create-room-watch','client':'alice-client','map':'sala-nova','kind':'map.create','value':new_map}
        status,created=self.call(self.base+'/room-exchange',{'map':'','cursors':{},'operations':[op]},'alice')
        self.assertEqual(status,200);self.assertGreaterEqual(created['revision'],2)
        thread.join(2);self.assertFalse(thread.is_alive());self.assertEqual(result['status'],200)
        self.assertLess(result['elapsed'],1.5);self.assertGreaterEqual(result['payload']['revision'],2)

    def test_room_watch_timeout_does_not_create_project_version(self):
        begin=time.monotonic()
        status,payload=self.call(self.base+'/room-watch',
            {'map':'rua','cursors':{'rua':0},'waitMs':300},'reader')
        self.assertEqual(status,200);self.assertGreaterEqual(time.monotonic()-begin,.20)
        self.assertEqual(payload['operations'],[]);self.assertEqual(payload['revision'],1)
        with self.store.connect() as db:
            self.assertEqual(db.execute('SELECT count(*) FROM versions WHERE project=?',(self.pid,)).fetchone()[0],1)

    def test_room_watch_is_not_woken_by_another_project(self):
        other=project();other['projectId']='other-local';other['projectName']='Outro projeto'
        status,created=self.call('projects',{'payload':other},'admin');self.assertEqual(status,200)
        other_base='projects/'+created['id']
        result={};started=threading.Event()
        def watch():
            started.set();begin=time.monotonic()
            status,payload=self.call(self.base+'/room-watch',
                {'map':'rua','cursors':{'rua':0},'waitMs':3000},'bob')
            result['status']=status;result['payload']=payload;result['elapsed']=time.monotonic()-begin
        thread=threading.Thread(target=watch);thread.start();started.wait(1);time.sleep(.12)
        unrelated=[{'id':'other-live','client':'other','map':'rua','kind':'region.cells',
                    'changes':[{'x':1,'y':1,'value':3}]}]
        self.assertEqual(self.call(other_base+'/room-exchange',
            {'map':'rua','cursors':{'rua':0},'operations':unrelated},'alice')[0],200)
        time.sleep(.25);self.assertTrue(thread.is_alive())
        related=[{'id':'first-live','client':'first','map':'rua','kind':'region.cells',
                  'changes':[{'x':2,'y':2,'value':4}]}]
        self.assertEqual(self.room('alice',related)[0],200)
        thread.join(2);self.assertFalse(thread.is_alive());self.assertEqual(result['status'],200)
        self.assertTrue(any(item['operation']['id']=='first-live' for item in result['payload']['operations']))

    def test_session_survives_store_reopen(self):
        token=self.tokens['alice']
        reopened=Store(self.temp.name)
        result=reopened.dispatch('GET',['projects'],{},token)
        self.assertTrue(any(item['id']==self.pid for item in result['projects']))


    def test_restart_preserves_versions_and_users(self):
        again=Store(self.temp.name)
        self.assertEqual(len(again.users()),4)
        with again.connect() as db:self.assertEqual(db.execute('SELECT revision FROM projects').fetchone()[0],1)


    def test_portable_asset_paths_survive_normalization(self):
        payload=project()
        payload['tilesets'][0]['source']='Assets/Tilesets/pedra.png'
        payload['maps'][0]['map']['panoramaPath']='Assets/Panorama/ceu.png'
        cleaned=clean_payload(payload)
        self.assertEqual(cleaned['tilesets'][0]['source'],'Assets/Tilesets/pedra.png')
        self.assertEqual(cleaned['maps'][0]['map']['panoramaPath'],'Assets/Panorama/ceu.png')
        payload['tilesets'][0]['source']='C:/private/pedra.png'
        self.assertNotIn('source',clean_payload(payload)['tilesets'][0])

    def test_asset_blob_manifest_download_and_dedup(self):
        data=b'fake-png-binary-for-team-assets'
        digest=hashlib.sha256(data).hexdigest()
        status,check=self.call(self.base+'/asset-blobs-check',{'hashes':[digest]},'alice')
        self.assertEqual(status,200);self.assertEqual(check['missing'],[digest])
        status,_=self.call_blob(digest,data,'alice');self.assertEqual(status,200)
        status,check=self.call(self.base+'/asset-blobs-check',{'hashes':[digest]},'bob')
        self.assertEqual(status,200);self.assertEqual(check['missing'],[])
        asset={'id':'asset.tiles.pedra','path':'Assets/Tilesets/pedra.png','sha256':digest,
               'size':len(data),'type':'image','category':'tilesets','metadata':{}}
        operation={'id':'asset-op-1','client':'alice-client','kind':'asset.upsert','asset':asset}
        status,result=self.call(self.base+'/asset-exchange',{'cursor':0,'operations':[operation]},'alice')
        self.assertEqual(status,200);self.assertEqual(result['assetSeq'],1)
        status,result=self.call(self.base+'/asset-exchange',{'cursor':0,'operations':[]},'reader')
        self.assertEqual(status,200);self.assertEqual(result['operations'][0]['operation']['asset']['path'],'Assets/Tilesets/pedra.png')
        status,download=self.call_blob(digest,None,'reader')
        self.assertEqual(status,200);self.assertEqual(download,data)
        # Retry is idempotent and does not advance the sequence.
        status,result=self.call(self.base+'/asset-exchange',{'cursor':0,'operations':[operation]},'alice')
        self.assertEqual(status,200);self.assertEqual(result['assetSeq'],1)

    def test_asset_blob_cannot_be_downloaded_before_manifest_reference(self):
        data=b'unreferenced-blob'
        digest=hashlib.sha256(data).hexdigest()
        self.assertEqual(self.call_blob(digest,data,'alice')[0],200)
        self.assertEqual(self.call_blob(digest,None,'reader')[0],404)

    def test_resource_channel_merges_without_map_checkpoint_roundtrip(self):
        base_resources=copy.deepcopy(self.initial);base_resources.pop('maps')
        changed=copy.deepcopy(base_resources)
        changed['tilesets'].append({'id':'tiles-2','source':'Assets/Tilesets/new.png'})
        body={'cursor':0,'baseSeq':0,'id':'resources-1','client':'alice-client','resources':changed}
        status,result=self.call(self.base+'/resources-exchange',body,'alice')
        self.assertEqual(status,200);self.assertEqual(result['resourceSeq'],1);self.assertFalse(result['conflict'])
        status,bob=self.call(self.base+'/resources-exchange',{'cursor':0},'bob')
        self.assertEqual(status,200);self.assertEqual(bob['resources']['tilesets'][-1]['source'],'Assets/Tilesets/new.png')
        stale=copy.deepcopy(base_resources);stale['projectName']='stale'
        status,conflict=self.call(self.base+'/resources-exchange',
                                  {'cursor':0,'baseSeq':0,'id':'resources-stale','client':'bob-client','resources':stale},'bob')
        self.assertEqual(status,200);self.assertTrue(conflict['conflict']);self.assertEqual(conflict['resourceSeq'],1)

    def test_protocol9_legacy_write_routes_are_gone(self):
        payload={'revision':1,'payload':self.initial,'delta':{}}
        for route in ('lease','release','commit','commit-delta'):
            status,_=self.call(self.base+'/'+route,payload,'admin')
            self.assertEqual(status,404,route)

    def test_protocol9_multi_instance_channels_converge(self):
        blob=b'parallel-asset';digest=hashlib.sha256(blob).hexdigest()
        self.assertEqual(self.call_blob(digest,blob,'admin')[0],200)
        barrier=threading.Barrier(3);results=[];lock=threading.Lock()
        def run(fn):
            barrier.wait();value=fn()
            with lock:results.append(value)
        tile=[{'id':'parallel-tile','client':'instance-a','map':'rua','kind':'tile.cells','layer':'ground',
               'changes':[{'x':4,'y':5,'value':{'tiles':['parallel']}}]}]
        resource={'kind':'resource.set','path':['projectName'],'beforeExists':True,'before':'Teste','value':'Equipe'}
        asset={'id':'parallel-asset','path':'Assets/parallel.bin','sha256':digest,'size':len(blob),'type':'binary','category':'project','metadata':{}}
        threads=[
            threading.Thread(target=run,args=(lambda:self.room('alice',tile)[0],)),
            threading.Thread(target=run,args=(lambda:self.patch_resources('bob','parallel-resource',[resource])[0],)),
            threading.Thread(target=run,args=(lambda:self.call(self.base+'/asset-exchange',{'cursor':0,'operations':[{'id':'parallel-manifest','kind':'asset.upsert','client':'instance-c','asset':asset}]},'admin')[0],))]
        for thread in threads:thread.start()
        for thread in threads:thread.join(10)
        self.assertEqual(sorted(results),[200,200,200])
        current=self.call(self.base,user='reader')[1]
        self.assertEqual(current['payload']['projectName'],'Equipe')
        self.assertEqual(current['payload']['maps'][0]['layers'][0]['data2D'][5][4],{'tiles':['parallel']})
        manifest=self.call(self.base+'/asset-exchange',{'cursor':0},'reader')[1]
        self.assertEqual(manifest['operations'][0]['operation']['asset']['path'],'Assets/parallel.bin')

    def test_protocol9_simultaneous_instances_keep_operation_ids_isolated(self):
        barrier=threading.Barrier(2);responses=[];lock=threading.Lock()
        def paint(user,op_id,x):
            operation=[{'id':op_id,'client':user+'-instance','map':'rua','kind':'tile.cells','layer':'ground',
                        'changes':[{'x':x,'y':6,'value':{'tiles':[user]}}]}]
            barrier.wait();answer=self.room(user,operation)
            with lock:responses.append(answer)
        threads=[threading.Thread(target=paint,args=('alice','multi-a',2)),threading.Thread(target=paint,args=('bob','multi-b',8))]
        for thread in threads:thread.start()
        for thread in threads:thread.join(10)
        self.assertEqual([status for status,_ in responses],[200,200])
        ground=self.call(self.base,user='reader')[1]['payload']['maps'][0]['layers'][0]['data2D']
        self.assertEqual(ground[6][2],{'tiles':['alice']});self.assertEqual(ground[6][8],{'tiles':['bob']})
        self.assertEqual(self.room('alice')[1]['roomSeqs']['rua'],2)


class EditorCliManagement(unittest.TestCase):
    def test_editor_can_manage_people_without_opening_server_gui(self):
        with tempfile.TemporaryDirectory() as folder:
            script = str(Path(__file__).with_name('ludo_server.py'))
            created = subprocess.run(
                [sys.executable, script, '--data', folder, '--add-user', 'admin',
                 '--role', 'admin', '--password-stdin'],
                input='senha-editor-segura\n', text=True, capture_output=True, timeout=20)
            self.assertEqual(created.returncode, 0, created.stdout + created.stderr)
            listed = subprocess.run(
                [sys.executable, script, '--data', folder, '--list-users-json'],
                text=True, capture_output=True, timeout=20)
            self.assertEqual(listed.returncode, 0, listed.stdout + listed.stderr)
            self.assertEqual(json.loads(listed.stdout), [{'name':'admin','role':'admin'}])

            subprocess.run(
                [sys.executable, script, '--data', folder, '--add-user', 'alice',
                 '--role', 'editor', '--password-stdin'],
                input='senha-alice-segura\n', text=True, capture_output=True, timeout=20, check=True)
            removed = subprocess.run(
                [sys.executable, script, '--data', folder, '--remove-user', 'alice'],
                text=True, capture_output=True, timeout=20)
            self.assertEqual(removed.returncode, 0, removed.stdout + removed.stderr)
            self.assertEqual(Store(folder).users(), [{'name':'admin','role':'admin'}])

    def test_editor_stop_file_ends_detached_server(self):
        with tempfile.TemporaryDirectory() as folder:
            Store(folder).add_user('admin','senha-editor-segura','admin')
            with socket.socket() as probe:
                probe.bind(('127.0.0.1',0))
                port=probe.getsockname()[1]
            stop_file=Path(folder)/'editor.stop'
            script=str(Path(__file__).with_name('ludo_server.py'))
            process=subprocess.Popen(
                [sys.executable,'-u',script,'--data',folder,'--host','127.0.0.1',
                 '--port',str(port),'--stop-file',str(stop_file)],
                stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            try:
                ready=process.stdout.readline().strip()
                self.assertIn('API 1',ready)
                stop_file.write_text('stop\n',encoding='utf-8')
                process.wait(timeout=5)
                self.assertEqual(process.returncode,0)
                self.assertFalse(stop_file.exists())
            finally:
                if process.poll() is None:
                    process.kill();process.wait(timeout=5)
                if process.stdout:
                    process.stdout.close()



if __name__=='__main__':unittest.main(verbosity=2)
