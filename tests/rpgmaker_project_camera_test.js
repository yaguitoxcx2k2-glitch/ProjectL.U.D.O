// node tests/rpgmaker_project_camera_test.js /path/to/Project4 [plugin-under-test]
// Uses the supplied project's native map clamping, camera framing, manifest,
// initial player coordinates and real PNG dimensions. Rendering is mocked.
const {makeContext}=require('./rpgmaker_bridge_smoke_test');
const fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const assert=require('node:assert/strict');
const project=path.resolve(process.argv[2]);
const read=p=>fs.readFileSync(path.join(project,p),'utf8');
const system=JSON.parse(read('data/System.json'));
const manifest=JSON.parse(read('data/ludoMaps/Map001.json'));
const c=makeContext('mz',process.argv[3]);
c.Graphics.width=system.advanced.screenWidth;c.Graphics.height=system.advanced.screenHeight;
const native=read('js/rmmz_objects.js'),camera=read('js/plugins/LudoCameraSystem.js');
function between(text,start,end){const i=text.indexOf(start);assert.ok(i>=0,start);return text.slice(i,text.indexOf(end,i));}
vm.runInContext(`Number.prototype.clamp=function(a,b){return Math.max(a,Math.min(b,this));};
Number.prototype.mod=function(n){return ((this%n)+n)%n;};`,c);
vm.runInContext(between(native,'Game_Map.prototype.setDisplayPos =','Game_Map.prototype.parallaxOx'),c);
vm.runInContext(between(camera,'    function halfX(','    function currentCameraCenter'),c);
vm.runInContext(between(camera,'    function setDisplayFromCenter(','    function startTransition'),c);
vm.runInContext(between(camera,'    function parallaxAxisDelta(','    function cameraRenderState'),c);
let zoom=2;
Object.assign(c.$gameMap,{
  width:()=>manifest.width,height:()=>manifest.height,
  tileWidth:()=>manifest.tileWidth,tileHeight:()=>manifest.tileHeight,
  screenTileX:()=>c.Graphics.width/(manifest.tileWidth*zoom),
  screenTileY:()=>c.Graphics.height/(manifest.tileHeight*zoom),
  isLoopHorizontal:()=>false,isLoopVertical:()=>false,
  displayX(){return this._displayX||0;},displayY(){return this._displayY||0;}
});
c.ImageManager.loadBitmap=(folder,name)=>{
  const png=fs.readFileSync(path.join(project,folder,decodeURIComponent(name)+'.png'));
  return {width:png.readUInt32BE(16),height:png.readUInt32BE(20),
    _baseTexture:{scaleMode:0},addLoadListener(fn){fn();}};
};
c.LudoCameraSystem.renderState=()=>({zoom,
  parallaxTravelX:c.$gameMap._ludoParallaxCameraMotion?.x||0,
  parallaxTravelY:c.$gameMap._ludoParallaxCameraMotion?.y||0,
  displayPixelX:c.$gameMap.displayX()*manifest.tileWidth,
  displayPixelY:c.$gameMap.displayY()*manifest.tileHeight,
  viewportWidth:c.Graphics.width/zoom,viewportHeight:c.Graphics.height/zoom});
c.__setManifest({...manifest,chunks:[],dynamic:[]});
c.$dataMap.note='<LudoMap>';c.DataManager.loadMapData(1);new c.Scene_Map().isReady();
const ss=new c.Spriteset_Map();ss.createCharacters();
let checks=0;
for(const z of [2,1,.5,1.5,3,2,1]){
  zoom=z;
  c.updateParallaxCameraMotion({focusCenterX:system.startX,focusCenterY:system.startY},zoom);
  c.setDisplayFromCenter(system.startX,system.startY,zoom,0,0);
  ss.update();
  const view=c.LudoCameraSystem.renderState();
  for(const s of ss._ludoParallaxSprites){
    for(const [axis,display,repeat,period] of [
      ['x',view.displayPixelX,s._ludoItem.repeatX,s._ludoBitmap.width],
      ['y',view.displayPixelY,s._ludoItem.repeatY,s._ludoBitmap.height]]){
      let error=s[axis]+display-Number(s._ludoItem[axis]||0);
      if(repeat)error-=Math.round(error/period)*period;
      assert.ok(Math.abs(error)<1e-7,
        `${s._ludoItem.name}: ${axis} shifted ${error}px at initial ${zoom}x`);
      checks++;
    }
  }
}
console.log(`OK project initial-camera alignment: ${checks} checks, actual 32px manifest and native camera framing`);

// Movement at 2x must still drive depth on this map, even though the map is
// smaller than the unzoomed window. Fractions must survive without snapping.
zoom=2;
c.updateParallaxCameraMotion({focusCenterX:15,focusCenterY:8},zoom);
const before={...c.$gameMap._ludoParallaxCameraMotion};
c.updateParallaxCameraMotion({focusCenterX:15.01,focusCenterY:8.01},zoom);
assert.ok(Math.abs(c.$gameMap._ludoParallaxCameraMotion.x-before.x-.32)<1e-9);
assert.ok(Math.abs(c.$gameMap._ludoParallaxCameraMotion.y-before.y-.32)<1e-9);
c.setDisplayFromCenter(15.01,8.01,zoom,0,0);ss.update();
for(const s of ss._ludoParallaxSprites){
  const item=s._ludoItem,motion=c.$gameMap._ludoParallaxCameraMotion;
  for(const [axis,factor,display,repeat,period] of [
    ['x',item.factorX,c.$gameMap.displayX()*32,item.repeatX,s._ludoBitmap.width],
    ['y',item.factorY,c.$gameMap.displayY()*32,item.repeatY,s._ludoBitmap.height]]){
    let error=s[axis]+display-Number(item[axis]||0)-motion[axis]*(1-factor);
    if(repeat)error-=Math.round(error/period)*period;
    assert.ok(Math.abs(error)<1e-7,'Rendered layer did not consume camera travel');
  }
}
const traveled=JSON.stringify(c.$gameMap._ludoParallaxCameraMotion);
for(const width of [816,1280,1920]){
  c.Graphics.width=width;
  for(const z of [.5,1,2,3]){
    c.updateParallaxCameraMotion({focusCenterX:15.01,focusCenterY:8.01},z);
    assert.equal(JSON.stringify(c.$gameMap._ludoParallaxCameraMotion),traveled,
      'Zoom or resize was interpreted as camera travel');
  }
}
// A stationary camera at an edge must not move backgrounds with the player.
assert.equal(c.parallaxAxisDelta(10,20,1248,640,false),0);
assert.equal(c.parallaxAxisDelta(1247,1,1248,640,true),2);
// Motion is saveable and a repeated sync has no extra integration step.
c.$gameMap._ludoParallaxCameraMotion=JSON.parse(traveled);
c.updateParallaxCameraMotion({focusCenterX:15.01,focusCenterY:8.01},2);
assert.equal(JSON.stringify(c.$gameMap._ludoParallaxCameraMotion),traveled);
c.$gameMap._ludoParallaxCameraMotion=null;
c.updateParallaxCameraMotion({focusCenterX:20,focusCenterY:5},2);
assert.equal(c.$gameMap._ludoParallaxCameraMotion.x,0);
assert.equal(c.$gameMap._ludoParallaxCameraMotion.y,0);
console.log('OK camera travel: subpixel, zoom, resize, edges, loop wrap, save-state and re-entry');
