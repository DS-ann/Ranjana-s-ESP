const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');
const { WebSocketServer } = require('ws');
const path = require('path');
const fs = require('fs');

const app = express();
app.use(cors());
app.use(bodyParser.json());

// ===== Serve HTML/JS/manifest =====
app.use(express.static(path.join(__dirname, 'public')));

// ===== Data storage =====
const DATA_FILE = path.join(__dirname, 'data.json');
let data = {
  devices: Array(8).fill(false),   // relay states
  usageStats: Array(8).fill({ last: '--', today: 0, total: 0 }),
  wifiInfo: { wifiNum: 1, rssi: -50 }
};

// Load existing data from file
if(fs.existsSync(DATA_FILE)){
  try {
    const raw = fs.readFileSync(DATA_FILE);
    data = JSON.parse(raw);
    console.log('Loaded data from file');
  } catch(e){
    console.log('Error reading data.json, using defaults');
  }
}

// Save data helper
function saveData(){
  fs.writeFile(DATA_FILE, JSON.stringify(data,null,2), err=>{
    if(err) console.log('Error saving data:', err);
  });
}

// ===== HTTP endpoints =====
app.get('/api/status', (req, res) => {
  res.json({
    relays: data.devices,
    usageStats: data.usageStats,
    wifiNum: data.wifiInfo.wifiNum,
    wifiRSSI: data.wifiInfo.rssi
  });
});

app.post('/api/toggle', (req, res) => {
  const { relay, state } = req.body;
  data.devices[relay] = !!state;
  broadcastWS({ type: 'status', relays: data.devices, usageStats: data.usageStats, wifiNum: data.wifiInfo.wifiNum, rssi: data.wifiInfo.rssi });
  saveData();
  res.json({ success: true });
});

app.post('/api/setTimer', (req, res) => {
  // Timer logic can still be handled by ESP
  res.json({ success: true });
});

// ===== WebSocket Server =====
const wss = new WebSocketServer({ port: 3000 });
console.log('WebSocket server running on port 3000');

wss.on('connection', ws => {
  console.log('Client connected');
  ws.send(JSON.stringify({ type: 'status', relays: data.devices, usageStats: data.usageStats, wifiNum: data.wifiInfo.wifiNum, rssi: data.wifiInfo.rssi }));

  ws.on('message', message => {
    try {
      const msg = JSON.parse(message);
      if(msg.type === 'toggle'){
        const { relay, state } = msg;
        data.devices[relay] = !!state;
      } else if(msg.type === 'setTimer'){
        // optional: store timers
      }
      broadcastWS({ type: 'status', relays: data.devices, usageStats: data.usageStats, wifiNum: data.wifiInfo.wifiNum, rssi: data.wifiInfo.rssi });
      saveData();
    } catch(e){
      console.log('Invalid WS message', e);
    }
  });

  ws.on('close', () => console.log('Client disconnected'));
});

// ===== Broadcast helper =====
function broadcastWS(msg){
  const str = JSON.stringify(msg);
  wss.clients.forEach(client => {
    if(client.readyState === client.OPEN) client.send(str);
  });
}

// ===== Start HTTP server =====
const PORT = process.env.PORT || 8080;
app.listen(PORT, () => console.log(`HTTP server running on port ${PORT}`));