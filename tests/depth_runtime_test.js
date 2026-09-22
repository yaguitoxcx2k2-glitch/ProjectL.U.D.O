// node tests/depth_runtime_test.js — Runtime Contract v5 harness; does not replace an RPG Maker playtest.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
let manifest;
class Game_Map {
  mapId(){return 1;} tileWidth(){return 32;} tileHeight(){return 32;}
  displayX(){return 0;} displayY(){return 0;} setupParallax(){} isPassable(){return true;}
  roundXWithDirection(x,d){return x+(d===6?1:d===4?-1:0);}
  roundYWithDirection(y,d){return y+(d===2?1:d===8?-1:0);}
  isValid(x,y){return x>=0&&y>=0&&x<12&&y<12;}
  eventsXy(x,y){return this.events.filter(e=>e.pos(x,y));}
  eventsXyNt(x,y){return this.eventsXy(x,y);} isEventRunning(){return false;}
  constructor(){this.events=[];}
}
class Game_CharacterBase {
  constructor(){this.x=this._realX=0;this.y=this._realY=0;}
  pos(x,y){return this.x===x&&this.y===y;}
  isMapPassable(x,y,d){return context.$gameMap.isPassable(x,y,d);}
  isCollidedWithEvents(){return false;} canPassDiagonally(){return true;}
  screenX(){return (this._realX+.5)*32;} screenY(){return (this._realY+1)*32;}
  moveStraight(d){const map=context.$gameMap; const nx=map.roundXWithDirection(this.x,d),ny=map.roundYWithDirection(this.y,d);
    this.success=this.isMapPassable(this.x,this.y,d)&&!this.isCollidedWithEvents(nx,ny);
    if(this.success){this.x=nx;this.y=ny;}}
  isMovementSucceeded(){return this.success;}
}
class Game_Player extends Game_CharacterBase {
  followers(){return {_data:[],data:()=>[],visibleFollowers:()=>[]};}
  update(){} startMapEvent(){} isTransferring(){return true;} performTransfer(){}
  reserveTransfer(){} direction(){return 2;}
}
class Game_Interpreter { pluginCommand(){} }

class Game_Follower extends Game_CharacterBase {synchronize(x,y){this.x=this._realX=x;this.y=this._realY=y;}}
class Game_Event extends Game_CharacterBase {
  constructor(level){super();this.note=`<LudoLevel: ${level}>`;}
  event(){return {note:this.note};} isNormalPriority(){return true;} isTriggerIn(){return true;}
  start(){this.started=true;} checkEventTriggerTouch(){this.started=true;}
  isCollidedWithPlayerCharacters(){return true;}
}
class Sprite {
 constructor(){this.scale={x:1,y:1,set(x,y){this.x=x;this.y=y;}};this.anchor={set(){}};this.x=this.y=this.z=0;}
 setFrame(){}
}
class Sprite_Character extends Sprite {
 constructor(c){super();this._character=c;}
 update(){this.x=this._character.screenX();this.y=this._character.screenY();this.z=3;}
}
class Sprite_Destination extends Sprite {update(){}}
class Spriteset_Map {
 constructor(){this.children=[];this._tilemap={addChild:s=>this.children.push(s)};}
 createCharacters(){} update(){}
}
class Scene_Map {isReady(){return true;}}
const registeredCommands=new Map();
const PluginManager={
  parameters(){return {};},
  registerCommand(plugin,command,fn){registeredCommands.set(`${plugin}:${command}`,fn);}
};
const context={Game_Map,Game_CharacterBase,Game_Player,Game_Follower,Game_Event,Game_Interpreter,Sprite,Sprite_Character,Sprite_Destination,Spriteset_Map,Scene_Map,
  PluginManager,
  DataManager:{loadMapData(){},isMapLoaded:()=>true}, ImageManager:{loadBitmap:()=>({})},
  Graphics:{width:816,height:624},
  PIXI:{BLEND_MODES:{ADD:1,MULTIPLY:2,SCREEN:3,NORMAL:0},SCALE_MODES:{NEAREST:0}},performance:{now:()=>1000},console,
  XMLHttpRequest:class{open(){} overrideMimeType(){} send(){this.status=200;this.responseText=JSON.stringify(manifest);this.onload();}}};
context.$gameMap=new Game_Map();context.$gamePlayer=new Game_Player();context.$dataMap={note:'<LudoMap>'};
vm.createContext(context);
vm.runInContext(fs.readFileSync(require('node:path').join(__dirname,'../integrations/rpg-maker-mz/LudoMapSystem.js'),'utf8'),context);
const place=(c,x,y,level=0)=>{c.x=c._realX=x;c.y=c._realY=y;c._ludoDepthLevel=level;};
function load(depth=true){
 manifest={format:'ludo-map',version:depth?5:3,mapId:1,width:12,height:12,tileWidth:32,tileHeight:32,collision:Array(144).fill(0),
  renderOrderCount:3,textureFiltering:'nearest',editorSource:{legacy:true},tileEffects:[{type:'legacy'}],
  chunks:[{file:'ground',x:0,y:0,plane:'below',level:0},{file:'bridge',x:0,y:0,plane:'below',level:1}],
  dynamic:[{atlas:'a',frames:[[0,0,32,32]],width:32,height:32,x:0,y:0,mode:'below',level:0}]};
 if(depth)manifest.depth={enabled:true,scale:.9,startLevel:0,collision:[Array(144).fill(0),Array(144).fill(0)],support:Array(144).fill(0),transitions:[{x0:3,y0:5,x1:3,y1:2},{x0:8,y0:5,x1:8,y1:2}]};
 if(depth)for(let x=3;x<=8;x++)manifest.depth.support[2*12+x]=1;
 context.DataManager.loadMapData(1);assert.equal(new Scene_Map().isReady(),true);context.$gameMap.events=[];
}
let passed=0;
function test(name,fn){fn();passed++;console.log('OK',name);}
const p=context.$gamePlayer;
test('Runtime Contract v5 sanitizes editor-only and removed tile-effect payloads',()=>{load();const current=context.LudoMapSystem.currentManifest();assert.equal(current.version,5);assert.equal(current.editorSource,undefined);assert.equal(current.tileEffects,undefined);assert.equal(current.textureFiltering,'nearest');assert.equal(current.renderOrderCount,3);});
test('legacy map uses original passage and scale',()=>{load(false);place(p,4,2);assert(p.isMapPassable(4,2,6));const s=new Spriteset_Map();s.createCharacters();s.update();assert.equal(s.children[0].scale.x,1);});
test('upper edge blocks falling and floor passes below bridge',()=>{load();place(p,5,2,1);assert(!p.isMapPassable(5,2,2));place(p,5,2);assert(p.isMapPassable(5,2,2));});
test('stairs ascend, turn back, descend; no side exits',()=>{load();place(p,3,5);p.moveStraight(8);assert.equal(p.y,4);p._realY=p.y;assert(!p.isMapPassable(3,4,6));p.moveStraight(2);assert.equal(p._ludoDepthLevel,0);p._realY=p.y;for(let i=0;i<3;i++){p.moveStraight(8);p._realY=p.y;}assert.equal(p.y,2);assert.equal(p._ludoDepthLevel,1);p.moveStraight(6);assert.equal(p.x,4);place(p,3,2,1);for(let i=0;i<3;i++){p.moveStraight(2);p._realY=p.y;}assert.equal(p.y,5);assert.equal(p._ludoDepthLevel,0);});
test('cannot enter stairs from wrong level or diagonally',()=>{load();place(p,3,2,0);assert(!p.isMapPassable(3,2,2));assert(!p.canPassDiagonally(2,4,6,8));});
test('distinct collision grids honor direction',()=>{load();manifest.depth.collision[0][2*12+4]=2;context.DataManager.loadMapData(1);new Scene_Map().isReady();place(p,4,2,0);assert(!p.isMapPassable(4,2,6));place(p,4,2,1);assert(p.isMapPassable(4,2,6));});
test('runtime keeps 1:1 scale and separates depth by z without accumulation',()=>{load();place(p,5,2,1);const s=new Spriteset_Map();s.createCharacters();for(let i=0;i<100;i++)s.update();assert.equal(s.children[0].scale.x,1);assert.equal(s.children[1].scale.x,1);assert.equal(s.children[2].scale.x,1);assert.equal(s.children[0].x,0);assert(s.children[1].z>=10&&s.children[1].z<11);});
test('stair rendering keeps 1:1 perspective contract',()=>{load();place(p,3,4);p._realY=3.5;const sprite=new Sprite_Character(p);sprite.update();assert.equal(sprite.scale.x,1);assert.equal(sprite.z,13);});
test('events preserve 1:1 scale while z follows logical floor',()=>{load();place(p,5,2,1);const e=new Game_Event(0);place(e,2,2,0);const se=new Sprite_Character(e),sp=new Sprite_Character(p);for(let i=0;i<100;i++){se.update();sp.update();}assert.equal(se.scale.x,1);assert.equal(sp.scale.x,1);assert.equal(se.z,3);assert.equal(sp.z,13);});
test('events on another floor neither block nor trigger',()=>{load();place(p,4,2,1);const lower=new Game_Event(0),upper=new Game_Event(1);place(lower,5,2,0);place(upper,5,2,1);context.$gameMap.events=[lower];assert(!p.isCollidedWithEvents(5,2));p.startMapEvent(5,2,[0],true);assert(!lower.started);lower.checkEventTriggerTouch(4,2);assert(!lower.started);context.$gameMap.events.push(upper);assert(p.isCollidedWithEvents(5,2));p.startMapEvent(5,2,[0],true);assert(upper.started);});
test('upper event blocks the last stair step',()=>{load();place(p,3,3,0);const e=new Game_Event(1);place(e,3,2,1);context.$gameMap.events=[e];p.moveStraight(8);assert.equal(p.y,3);});
test('save reload on same map retains upper level',()=>{load();place(p,5,2,1);const saved=JSON.stringify(p);context.DataManager.loadMapData(1);new Scene_Map().isReady();Object.assign(p,JSON.parse(saved));const sp=new Sprite_Character(p);sp.update();assert.equal(sp.z,13);});
test('descending from second stair returns to lower floor',()=>{load();place(p,8,2,1);for(let i=0;i<3;i++){p.moveStraight(2);p._realY=p.y;}assert.equal(p.y,5);assert.equal(p._ludoDepthLevel,0);});
test('transfer resets floor; save serialization retains it',()=>{load();place(p,5,2,1);assert.equal(JSON.parse(JSON.stringify(p))._ludoDepthLevel,1);p.performTransfer();assert.equal(p._ludoDepthLevel,0);});
test('bad stair data rejected before rendering',()=>{load();manifest.depth.transitions.push({x0:0,y0:0,x1:2,y1:2});context.DataManager.loadMapData(1);assert.throws(()=>new Scene_Map().isReady(),/Escada invalida/);});
console.log(`${passed} contract tests passed.`);
