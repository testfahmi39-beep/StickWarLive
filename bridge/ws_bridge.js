const WebSocket = require("ws");

const wss = new WebSocket.Server({ port: 8765 });
console.log("StickWar WebSocket bridge listening on ws://0.0.0.0:8765");

wss.on("connection", (ws) => {
  ws.send(JSON.stringify({
    type: "status",
    connected: true,
    message: "Bridge ready (send {action:'connect', username:'...'})"
  }));

  ws.on("message", (raw) => {
    try {
      const cmd = JSON.parse(raw.toString());
      if (cmd.action === "connect") {
        ws.send(JSON.stringify({
          type: "status",
          connected: true,
          message: `Connected to @${cmd.username || "streamer"}`
        }));
      } else if (cmd.action === "disconnect") {
        ws.send(JSON.stringify({
          type: "status",
          connected: false,
          message: "Disconnected from live stream"
        }));
      }
    } catch (e) {
      ws.send(JSON.stringify({
        type: "error",
        message: "Invalid command JSON: " + e.message
      }));
    }
  });
});
