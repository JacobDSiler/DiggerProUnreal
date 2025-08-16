import 'dotenv/config';
import express from 'express';
import http from 'http';
import cors from 'cors';
import { Server as SocketIOServer } from 'socket.io';

const PORT = process.env.PORT || 3000;
const ALLOWED_ORIGIN = process.env.ALLOWED_ORIGIN || '*';
const origins = ALLOWED_ORIGIN === '*' ? true : ALLOWED_ORIGIN.split(',').map(s => s.trim());

const app = express();
app.use(cors({ origin: origins, credentials: true }));

// Simple health check for you / for Render later
app.get('/health', (req, res) => res.json({ ok: true, ver: '1.0.0' }));

const server = http.createServer(app);
const io = new SocketIOServer(server, {
  cors: { origin: origins, methods: ['GET', 'POST'] },
  path: '/socket.io' // keep explicit
});

// Minimal rooms/presence/brush relay
io.on('connection', (socket) => {
  socket.on('join', (room) => {
    if (!room) return;
    socket.join(room);
    socket.to(room).emit('presence', { type: 'join', id: socket.id });
  });

  socket.on('brush', ({ room, payload }) => {
    if (!room) return;
    socket.to(room).emit('brush', payload);
  });

  socket.on('leave', (room) => {
    if (!room) return;
    socket.leave(room);
    socket.to(room).emit('presence', { type: 'leave', id: socket.id });
  });

  socket.on('disconnecting', () => {
    for (const room of socket.rooms) {
      if (room !== socket.id) {
        socket.to(room).emit('presence', { type: 'leave', id: socket.id });
      }
    }
  });
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`DiggerConnect lobby listening on :${PORT}, origins: ${ALLOWED_ORIGIN}`);
});
