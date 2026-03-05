const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');
const moment = require('moment');
const fs = require('fs');
const path = require('path');

const app = express();
const PORT = process.env.PORT || 3000;

app.use(cors());
app.use(bodyParser.json());

// Serve static files from the 'public' folder
app.use(express.static(path.join(__dirname, 'public')));

const DATA_FILE = path.join(__dirname,'data.json');

let relays = Array(8).fill(false);
let timers = Array(8).fill(0);
let usageStats = Array(8).fill(0).map(()=>({last:0,today:0,total:0}));
let commandQueue = [];
let lastResetDate = moment().format("YYYY-MM-DD");

// Load data from JSON
function loadData(){
  if(fs.existsSync(DATA_FILE)){
    const data = JSON.parse(fs.readFileSync(DATA_FILE));
    relays = data.relays || relays;
    timers = data.timers || timers;
    usageStats = data.usageStats || usageStats;
    commandQueue = data.commandQueue || commandQueue;
    lastResetDate = data.lastResetDate || lastResetDate;
  }
}

// Save data to JSON
function saveData(){
  fs.writeFileSync(DATA_FILE, JSON.stringify({relays,timers,usageStats,commandQueue,lastResetDate}, null,2));
}

// Update timers and daily reset
function updateTimers(){
  const today = moment().format("YYYY-MM-DD");
  if(today !== lastResetDate){
    usageStats.forEach(u => u.today = 0);
    lastResetDate = today;
  }
  for(let i=0;i<8;i++){
    if(timers[i]>0){
      timers[i]--;
      usageStats[i].today++;
      usageStats[i].total++;
      usageStats[i].last = Math.floor(Date.now()/1000);
      if(timers[i]<=0) relays[i]=false;
    }
  }
  saveData();
}

// ===== API Endpoints =====

// Get current status
app.get('/api/status', (req,res)=>{
  updateTimers();
  res.json({relays,usageStats,usageToday:timers});
});

// Toggle relay
app.post('/api/toggle', (req,res)=>{
  const {relay,state} = req.body;
  if(relay>=0 && relay<8){
    relays[relay]=state?true:false;
    if(state) timers[relay]=0;
    commandQueue.push({id:relay,state:relays[relay],timer:timers[relay],timestamp:Math.floor(Date.now()/1000)});
    saveData();
    res.json({status:"ok"});
  }else res.status(400).json({status:"error"});
});

// Set timer
app.post('/api/setTimer', (req,res)=>{
  const {id,sec} = req.body;
  if(id>=0 && id<8){
    timers[id]=sec;
    relays[id]=true;
    commandQueue.push({id,state:true,timer:sec,timestamp:Math.floor(Date.now()/1000)});
    saveData();
    res.json({status:"ok"});
  }else res.status(400).json({status:"error"});
});

// Get queued commands (ESP fetch)
app.get('/api/getCommand',(req,res)=>res.json(commandQueue));

// Acknowledge command executed
app.get('/api/ackCommand',(req,res)=>{
  const {id} = req.query;
  const index = commandQueue.findIndex(c=>c.id==id);
  if(index>=0) commandQueue.splice(index,1);
  saveData();
  res.json({status:"ok"});
});

// Load data and start server
loadData();
app.listen(PORT,()=>console.log(`Server running on port ${PORT}`));