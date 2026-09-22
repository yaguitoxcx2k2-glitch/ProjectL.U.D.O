// node tests/rpgmaker_bridge_smoke_test.js — load/command smoke test for both RPG Maker bridges.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');

function makeContext(target, pluginOverride) {
  let manifestResponse = null;
  class Game_Map {
    mapId(){return 1;} tileWidth(){return 48;} tileHeight(){return 48;}
    displayX(){return 0;} displayY(){return 0;} setupParallax(){} isPassable(){return true;}
    roundXWithDirection(x,d){return x+(d===6?1:d===4?-1:0);}
    roundYWithDirection(y,d){return y+(d===2?1:d===8?-1:0);}
    isValid(){return true;} regionId(){return 0;} eventsXy(){return [];} eventsXyNt(){return [];}
    isEventRunning(){return false;}
  }
  class Game_CharacterBase {
    constructor(){this.x=this.y=this._realX=this._realY=0;this._ludoDepthLevel=0;}
    isMapPassable(){return true;} isCollidedWithEvents(){return false;} canPassDiagonally(){return true;}
    moveStraight(){} moveDiagonally(){} screenX(){return 0;} screenY(){return 0;}
  }
  class Game_Player extends Game_CharacterBase {
    constructor(){super();this._followers=[new Game_CharacterBase()];}
    followers(){return {data:()=>this._followers,visibleFollowers:()=>this._followers};}
    update(){} startMapEvent(){} performTransfer(){} isTransferring(){return false;}
  }
  class Game_Follower extends Game_CharacterBase { synchronize(){} }
  class Game_Event extends Game_CharacterBase {
    event(){return {note:''};} isNormalPriority(){return true;} isTriggerIn(){return true;}
    start(){} checkEventTriggerTouch(){} isCollidedWithPlayerCharacters(){return false;}
  }
  class Game_Interpreter { pluginCommand(){} }
  class Sprite {
    constructor(bitmap){this.bitmap=bitmap;this.children=[];this.scale={x:1,y:1,set(x,y){this.x=x;this.y=y;}};this.anchor={x:0,y:0,set(x,y){this.x=x;this.y=y;}};this.opacity=255;this.z=0;this.x=this.y=0;}
    addChild(child){this.children.push(child);return child;}
    removeChild(child){this.children=this.children.filter(item=>item!==child);return child;}
    get width(){return Math.max(1,Number(this.bitmap&&this.bitmap.width||100))*Math.abs(this.scale.x);}
    get height(){return Math.max(1,Number(this.bitmap&&this.bitmap.height||100))*Math.abs(this.scale.y);}
    setFrame(...frame){this.frame=frame;}
  }
  class Bitmap { constructor(width,height){this.width=width;this.height=height;} fillAll(color){this.color=color;} }
  class Sprite_Character extends Sprite { update(){} }
  class Sprite_Destination extends Sprite { update(){} }
  class Spriteset_Map { constructor(){this._tilemap={children:[],addChild(sprite){this.children.push(sprite);}};} createCharacters(){} update(){} }
  class Scene_Map { isReady(){return true;} }

  const registered = new Map();
  const PluginManager = {
    parameters(){return {};},
    registerCommand(plugin, command, fn){registered.set(`${plugin}:${command}`, fn);}
  };
  const context = {
    Game_Map, Game_CharacterBase, Game_Player, Game_Follower, Game_Event, Game_Interpreter,
    Sprite, Bitmap, Sprite_Character, Sprite_Destination, Spriteset_Map, Scene_Map,
    PluginManager,
    DataManager:{loadMapData(){},isMapLoaded:()=>true},
    ImageManager:{loadBitmap:()=>({width:960,height:540,_baseTexture:{scaleMode:0},addLoadListener(fn){fn();}})},
    Graphics:{width:816,height:624},
    PIXI:{BLEND_MODES:{ADD:1,MULTIPLY:2,SCREEN:3,NORMAL:0},SCALE_MODES:{NEAREST:0,LINEAR:1},
      Filter:class{constructor(vertex,fragment,uniforms){this.fragment=fragment;this.uniforms=uniforms;}}},
    performance:{now:()=>1000}, console, XMLHttpRequest:class{
      open(){} overrideMimeType(){} send(){this.status=200;this.responseText=JSON.stringify(manifestResponse);this.onload();}
    },
    $dataMap:{note:''}
  };
  context.$gameMap = new Game_Map();
  context.$gamePlayer = new Game_Player();
  if (target === 'mz') {
    context.LudoCameraSystem = {renderState:()=>({
      zoom:2, displayPixelX:96, displayPixelY:48,
      viewportWidth:408, viewportHeight:312
    })};
  }
  context.window = context;
  context.__registered = registered;
  context.__setManifest = value => { manifestResponse = value; };
  vm.createContext(context);
  const pluginPath = pluginOverride || path.join(__dirname, `../integrations/rpg-maker-${target}/LudoMapSystem.js`);
  vm.runInContext(fs.readFileSync(pluginPath, 'utf8'), context, {filename: pluginPath});
  return context;
}

for (const target of ['mz','mv']) {
  const context = makeContext(target);
  assert.ok(context.LudoMapSystem, `${target}: public LudoMapSystem API missing`);
  assert.ok(context.LudoMapDepth, `${target}: public depth API missing`);
  if (target === 'mz') {
    const command = context.__registered.get('LudoMapSystem:SetPlayerLevel');
    assert.equal(typeof command, 'function', 'MZ plugin command was not registered');
    command({level:'1',followers:'true'});
  } else {
    const interpreter = new context.Game_Interpreter();
    interpreter.pluginCommand('LudoMap', ['SetPlayerLevel','1','true']);
  }
  assert.equal(context.$gamePlayer._ludoDepthLevel, 1, `${target}: SetPlayerLevel failed`);
  assert.equal(context.$gamePlayer.followers().data()[0]._ludoDepthLevel, 1, `${target}: follower level sync failed`);

  context.__setManifest({format:'ludo-map',version:5,mapId:1,width:20,height:12,tileWidth:48,tileHeight:48,
    chunks:[],dynamic:[],collision:[],parallaxLayers:[
      {file:'sky-back.png',x:0,y:0,factorX:.25,factorY:.2,speedX:8,speedY:0,repeatX:true,repeatY:false,opacity:1,plane:'below',parallaxOrder:0,
       oscillationX:4,oscillationY:2,oscillationSpeed:.2,smoothMotion:true,effect:'underwater',effectStrength:.6,effectSpeed:1,
       animation:{frameWidth:240,frameHeight:540,frameCount:4,sheetColumns:4,fps:8,pingPong:true}},
      {file:'sky-front.png',x:0,y:0,factorX:.5,factorY:.35,speedX:3,speedY:0,repeatX:false,repeatY:false,opacity:1,plane:'below',parallaxOrder:1}
    ]});
  context.$dataMap.note = '<LudoMap>';
  context.DataManager.loadMapData(1);
  assert.equal(new context.Scene_Map().isReady(), true, `${target}: parallax manifest did not become ready`);
  const spriteset = new context.Spriteset_Map();
  spriteset.createCharacters();
  assert.equal(spriteset._ludoParallaxSprites.length, 2, `${target}: parallax sprites were not created`);
  assert.ok(spriteset._ludoParallaxSprites[0]._ludoRepeatChildren.length >= 3, `${target}: repeated parallax did not create seamless copies`);
  assert.ok(spriteset._ludoParallaxSprites[1].z > spriteset._ludoParallaxSprites[0].z,
    `${target}: parallax stack order was reversed`);
  spriteset.update();
  assert.equal(spriteset._ludoParallaxSprites[0].visible, true, `${target}: parallax was incorrectly culled at camera edge`);
  if (target === 'mz') {
    assert.equal(typeof spriteset.syncLudoCameraTransform, 'function',
      'MZ: official camera synchronization hook is missing');
    assert.equal(spriteset._ludoParallaxBackground.scale.x, 410,
      'MZ: parallax background ignored the zoom-aware viewport');
    assert.ok(spriteset._ludoParallaxSprites[0]._ludoEffectFilter, 'MZ: selected-layer effect was not created');
    assert.equal(spriteset._ludoParallaxSprites[0].roundPixels, false, 'MZ: parallax root still rounds subpixel movement');
    assert.equal(spriteset._ludoParallaxSprites[0]._ludoRepeatChildren[0].roundPixels, false,
      'MZ: repeated parallax copy still rounds subpixel movement');
    assert.equal(spriteset._ludoParallaxSprites[0]._ludoBitmap._baseTexture.scaleMode, 1,
      'MZ: moving visual layer did not enable linear sampling');
    assert.deepEqual(spriteset._ludoParallaxSprites[0]._ludoRepeatChildren[0].frame,
      [0,0,240,540], 'MZ: animated visual layer frame was not applied');
  }
  console.log(`OK RPG Maker ${target.toUpperCase()} bridge loaded and SetPlayerLevel works`);
}

// 39x15 map with native 32px tiles: centering is independent of depth.
{
  const c=makeContext('mz');
  c.Graphics.width=1280;c.Graphics.height=720;
  c.$gameMap.tileWidth=c.$gameMap.tileHeight=()=>32;
  let loop=false;
  c.$gameMap.isLoopHorizontal=c.$gameMap.isLoopVertical=()=>loop;
  const layers=[0,.1,.3,1,-1].map(f=>({file:'layer.png',x:64,y:80,
    factorX:f,factorY:f,opacity:1,plane:'below'}));
  c.__setManifest({format:'ludo-map',version:5,mapId:1,width:39,height:15,
    tileWidth:32,tileHeight:32,chunks:[],dynamic:[],collision:[],parallaxLayers:layers});
  c.$dataMap.note='<LudoMap>';
  c.DataManager.loadMapData(1);
  new c.Scene_Map().isReady();
  const ss=new c.Spriteset_Map();ss.createCharacters();
  for(const zoom of [.5,1,2]){
    const vw=1280/zoom,vh=720/zoom;
    const cx=(1248-vw)/2,cy=(480-vh)/2;
    c.LudoCameraSystem.renderState=()=>({zoom,viewportWidth:vw,viewportHeight:vh,
      displayPixelX:cx,displayPixelY:cy,parallaxTravelX:0,parallaxTravelY:0});
    ss.update();
    for(const s of ss._ludoParallaxSprites){
      assert.equal(s.x,64-cx);assert.equal(s.y,80-cy);
    }
  }
  // At a fixed world camera center, resize/zoom must not change a layer's
  // world position relative to the ground. Also cross both size boundaries.
  for(const width of [816,1248,1280,1920]){
    c.Graphics.width=width;
    for(const zoom of [.5,1,1.499,1.5,1.501,2,3]){
      const vw=width/zoom,vh=720/zoom;
      const dx=700-vw/2,dy=300-vh/2;
      c.LudoCameraSystem.renderState=()=>({zoom,viewportWidth:vw,viewportHeight:vh,
        displayPixelX:dx,displayPixelY:dy,parallaxTravelX:76,parallaxTravelY:60});
      ss.update();
      for(const s of ss._ludoParallaxSprites){
        const f=s._ludoItem.factorX;
        const travelX=76;
        assert.ok(Math.abs(s.x+dx-(64+travelX*(1-f)))<1e-9,
          `X world alignment changed at ${width}px / ${zoom}x / factor ${f}`);
        assert.ok(Math.abs(s.y+dy-(80+60*(1-f)))<1e-9,
          `Y world alignment changed at ${width}px / ${zoom}x / factor ${f}`);
      }
    }
  }
  loop=true;
  c.Graphics.width=1280;
  c.LudoCameraSystem.renderState=()=>({zoom:1,viewportWidth:1280,viewportHeight:720,
    displayPixelX:100,displayPixelY:50});
  ss.update();
  for(const s of ss._ludoParallaxSprites){
    assert.equal(s.x,64-100*s._ludoItem.factorX);
    assert.equal(s.y,80-50*s._ludoItem.factorY);
  }
  // A scrollable non-looping map retains depth movement at native zoom.
  loop=false;c.Graphics.width=816;c.Graphics.height=312;
  c.LudoCameraSystem.renderState=()=>({zoom:1,viewportWidth:816,viewportHeight:312,
    displayPixelX:100,displayPixelY:50});
  ss.update();
  for(const s of ss._ludoParallaxSprites){
    assert.ok(Math.abs(s.x-(64-100*s._ludoItem.factorX))<1e-9);
    assert.ok(Math.abs(s.y-(80-50*s._ludoItem.factorY))<1e-9);
  }
  console.log('OK MZ 32px parallax centering at zoom 0.5/1/2 and looping scroll');
}

module.exports={makeContext};
