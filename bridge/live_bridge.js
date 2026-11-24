const dgram = require("dgram");
const fs = require("fs");
const path = require("path");

const GAME_HOST = "127.0.0.1";
const GAME_PORT = 9090;      // Sends spawns TO the game
const CONTROL_PORT = 9091;   // Receives @username FROM the game's Main Menu

const gameClient = dgram.createSocket("udp4");
const controlServer = dgram.createSocket("udp4");

const catalogPath = path.join(__dirname, "..", "mods", "units.json");
const catalog = JSON.parse(fs.readFileSync(catalogPath, "utf8")).units;

let activeSimTimer = null;

function sendToGame(obj) {
  const payload = Buffer.from(JSON.stringify(obj));
  gameClient.send(payload, GAME_PORT, GAME_HOST);
}

function sendSpawn(team, unitId, user) {
  sendToGame({ type: "spawn", team, unit: unitId, user });
  console.log(`Spawn -> [${team.toUpperCase()}] ${unitId} (${user})`);
}

function handleLiveEvent({ giftName, comment, username }) {
  const viewer = username || "Viewer";
  const text = (giftName || comment || "").toLowerCase();

  for (const u of catalog) {
    if (u.allyTrigger && text.includes(u.allyTrigger.toLowerCase())) {
      sendSpawn("ally", u.id, viewer);
      return;
    }
    if (u.enemyTrigger && text.includes(u.enemyTrigger.toLowerCase())) {
      sendSpawn("enemy", u.id, viewer);
      return;
    }
  }
}

function connectStreamForUser(username) {
  console.log(`Connecting live listener for @${username}...`);
  if (activeSimTimer) clearInterval(activeSimTimer);

  // --- PLUG YOUR REAL LIVE STREAM CONNECTOR HERE ---
  // Example using your TikTok connector for `username`:
  // tiktokConnection = new WebcastPushConnection(username);
  // tiktokConnection.connect().then(() => {
  //   sendToGame({ type: "status", connected: true, message: `Connected to @${username}` });
  // });
  // tiktokConnection.on("gift", (data) => handleLiveEvent({ giftName: data.giftName, username: data.uniqueId }));
  // tiktokConnection.on("chat", (data) => handleLiveEvent({ comment: data.comment, username: data.uniqueId }));

  // Built-in simulator fallback so you can test immediately after typing a username:
  let i = 0;
  const viewers = ["Aiman_MY", "Siti_Live", "Danish99", "Farah_Stream"];
  activeSimTimer = setInterval(() => {
    const u = catalog[i % catalog.length];
    sendSpawn(i % 2 === 1 ? "enemy" : "ally", u.id, viewers[i % viewers.length]);
    i++;
  }, 4500);
}

controlServer.on("message", (msg) => {
  try {
    const cmd = JSON.parse(msg.toString());
    if (cmd.action === "connect" && cmd.username) {
      connectStreamForUser(cmd.username);
    } else if (cmd.action === "disconnect") {
      console.log("Disconnected stream listener.");
      if (activeSimTimer) clearInterval(activeSimTimer);
    }
  } catch (e) {}
});

controlServer.bind(CONTROL_PORT, "127.0.0.1", () => {
  console.log(`Bridge listening for Main Menu @username on 127.0.0.1:${CONTROL_PORT}`);
});
