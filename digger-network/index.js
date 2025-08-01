const express = require('express');
const http = require('http');
const { Server } = require('socket.io');

const app = express();
const server = http.createServer(app);
const io = new Server(server, {
  cors: {
    origin: '*',
    methods: ["GET", "POST"]
  }
});

const PORT = 3001;

let lobbies = {}; // { lobbyId: { name: "...", users: [user1, user2], ... } }

io.on('connection', (socket) => {
  console.log(`✅ Client connected: ${socket.id}`);

  // Handle lobby creation (matches your UE5 CreateLobby method)
  socket.on('createLobby', (data) => {
    console.log('📝 Create lobby request:', data);
    
    const lobbyName = data.lobbyName || 'Unnamed Lobby';
    const lobbyId = `lobby_${Date.now()}_${Math.random().toString(36).substr(2, 9)}`;
    
    // Create the lobby
    lobbies[lobbyId] = {
      name: lobbyName,
      users: [],
      createdBy: socket.id,
      createdAt: new Date()
    };

    console.log(`📌 Lobby '${lobbyName}' created with ID: ${lobbyId}`);

    // Send confirmation back to the creator
    socket.emit('lobbyCreated', {
      lobbyId: lobbyId,
      lobbyName: lobbyName,
      success: true
    });

    // Broadcast updated lobby list to all clients
    io.emit('lobbyListUpdated', Object.keys(lobbies).map(id => ({
      lobbyId: id,
      lobbyName: lobbies[id].name,
      userCount: lobbies[id].users.length
    })));
  });

  // Handle joining a lobby (matches your UE5 JoinLobby method)
  socket.on('joinLobby', (data) => {
    console.log('🚪 Join lobby request:', data);
    
    const lobbyId = data.lobbyId;
    const userName = data.userName || `User_${socket.id.substr(0, 6)}`;

    if (!lobbies[lobbyId]) {
      console.log(`❌ Lobby ${lobbyId} not found`);
      socket.emit('lobbyJoinFailed', {
        lobbyId: lobbyId,
        error: 'Lobby not found'
      });
      return;
    }

    // Add user to lobby if not already there
    if (!lobbies[lobbyId].users.find(u => u.socketId === socket.id)) {
      lobbies[lobbyId].users.push({
        socketId: socket.id,
        userName: userName,
        joinedAt: new Date()
      });
    }

    // Join the socket room
    socket.join(lobbyId);
    socket.data = { lobbyId, userName };

    console.log(`👤 ${userName} joined lobby '${lobbies[lobbyId].name}' (${lobbyId})`);

    // Confirm join to the user
    socket.emit('lobbyJoined', {
      lobbyId: lobbyId,
      lobbyName: lobbies[lobbyId].name,
      userName: userName,
      success: true
    });

    // Notify others in the lobby
    socket.to(lobbyId).emit('userJoined', {
      userName: userName,
      lobbyId: lobbyId,
      lobbyName: lobbies[lobbyId].name
    });

    // Send updated lobby list to all clients
    io.emit('lobbyListUpdated', Object.keys(lobbies).map(id => ({
      lobbyId: id,
      lobbyName: lobbies[id].name,
      userCount: lobbies[id].users.length
    })));
  });

  // Handle brush strokes (keeping your existing logic)
  socket.on('sendBrushStroke', ({ lobbyId, strokeData }) => {
    console.log(`🎨 Brush stroke in lobby ${lobbyId}:`, strokeData);
    socket.to(lobbyId).emit('receiveBrushStroke', strokeData);
  });

  // Handle getting lobby list
  socket.on('getLobbyList', () => {
    socket.emit('lobbyListUpdated', Object.keys(lobbies).map(id => ({
      lobbyId: id,
      lobbyName: lobbies[id].name,
      userCount: lobbies[id].users.length
    })));
  });

  // Handle disconnect
  socket.on('disconnect', () => {
    const { lobbyId, userName } = socket.data || {};
    console.log(`❌ Disconnected: ${userName || socket.id}`);

    if (lobbyId && lobbies[lobbyId]) {
      // Remove user from lobby
      lobbies[lobbyId].users = lobbies[lobbyId].users.filter(u => u.socketId !== socket.id);
      
      // Delete lobby if empty
      if (lobbies[lobbyId].users.length === 0) {
        delete lobbies[lobbyId];
        console.log(`🧹 Lobby '${lobbyId}' deleted (empty)`);
      }

      // Notify others in the lobby
      if (lobbies[lobbyId]) {
        socket.to(lobbyId).emit('userLeft', {
          userName: userName,
          lobbyId: lobbyId
        });
      }

      // Send updated lobby list to all clients
      io.emit('lobbyListUpdated', Object.keys(lobbies).map(id => ({
        lobbyId: id,
        lobbyName: lobbies[id].name,
        userCount: lobbies[id].users.length
      })));
    }
  });

  // Send initial lobby list when client connects
  socket.emit('lobbyListUpdated', Object.keys(lobbies).map(id => ({
    lobbyId: id,
    lobbyName: lobbies[id].name,
    userCount: lobbies[id].users.length
  })));
});

server.listen(PORT, () => {
  console.log(`🚀 Digger Networking Server running at http://localhost:${PORT}`);
  console.log(`📡 Socket.IO server ready for connections`);
});