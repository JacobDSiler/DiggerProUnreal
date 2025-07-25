const express = require('express');
const http = require('http');
const { Server } = require('socket.io');

const app = express();
const server = http.createServer(app);
const io = new Server(server, {
  cors: {
    origin: '*'
  }
});

const PORT = 3001;

let lobbies = {}; // { lobbyName: { users: [user1, user2], ... } }

io.on('connection', (socket) => {
  console.log(`✅ Client connected: ${socket.id}`);

  socket.on('joinLobby', ({ lobbyName, userName }) => {
    if (!lobbies[lobbyName]) {
      lobbies[lobbyName] = { users: [] };
      console.log(`📌 Lobby '${lobbyName}' created.`);
    }

    // Avoid duplicates
    if (!lobbies[lobbyName].users.includes(userName)) {
      lobbies[lobbyName].users.push(userName);
    }

    socket.join(lobbyName);
    socket.data = { lobbyName, userName };

    console.log(`👤 ${userName} joined lobby '${lobbyName}'`);

    // Notify lobby
    io.to(lobbyName).emit('userJoined', {
      userName,
      lobbyName
    });

    // Send updated lobby list to all
    io.emit('lobbyListUpdated', Object.keys(lobbies));
  });

  socket.on('sendBrushStroke', ({ lobbyName, strokeData }) => {
    console.log(`🎨 Brush stroke in ${lobbyName}:`, strokeData);
    socket.to(lobbyName).emit('receiveBrushStroke', strokeData);
  });

  socket.on('disconnect', () => {
    const { lobbyName, userName } = socket.data || {};
    console.log(`❌ Disconnected: ${userName || socket.id}`);

    if (lobbyName && lobbies[lobbyName]) {
      lobbies[lobbyName].users = lobbies[lobbyName].users.filter(u => u !== userName);
      if (lobbies[lobbyName].users.length === 0) {
        delete lobbies[lobbyName];
        console.log(`🧹 Lobby '${lobbyName}' deleted (empty)`);
      }
      io.emit('lobbyListUpdated', Object.keys(lobbies));
    }
  });
});

server.listen(PORT, () => {
  console.log(`🚀 Digger Networking Server running at http://localhost:${PORT}`);
});
