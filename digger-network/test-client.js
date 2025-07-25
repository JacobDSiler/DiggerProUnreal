const { io } = require("socket.io-client");

const userName = process.argv[2] || "JUser";
const socket = io("http://localhost:3001");

socket.on("connect", () => {
  console.log(`✅ Connected as ${userName} (${socket.id})`);

  // Join or create lobby
  socket.emit("joinLobby", {
    lobbyName: "TestLobby",
    userName
  });

  // Confirm other users join
  socket.on("userJoined", ({ userName, lobbyName }) => {
    console.log(`👋 ${userName} joined ${lobbyName}`);
  });

  // Confirm strokes sent by others
  socket.on("receiveBrushStroke", (data) => {
    console.log("🖌 Received Brush Stroke:", data);
  });

  // Send a test brush stroke
  socket.emit("sendBrushStroke", {
    lobbyName: "TestLobby",
    strokeData: {
      type: "sphere",
      radius: 75,
      position: { x: 100 + Math.random() * 50, y: 200, z: 50 }
    }
  });
});

socket.on("disconnect", () => {
  console.log(`❌ Disconnected: ${userName}`);
});
