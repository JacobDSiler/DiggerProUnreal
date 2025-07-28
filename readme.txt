# 🛠️ DIGGER for Unreal Engine 5

Welcome to **DIGGER**, a powerful voxel-based terrain and mesh editing tool for Unreal Engine 5 — currently in **private development** by a solo dev, J. (a.k.a. Kick Patowski of code when the baby’s napping 🍼👨‍💻).

This plugin is being actively developed and tested for:
- ✏️ Sculptable voxel terrain
- 🕳️ Dynamic holes, overhangs, and islands
- ⚙️ Procedural mesh generation with marching cubes
- 📡 Optional multiplayer (Editor and PIE) support via Socket.IO and Unreal native networking.

> 🚧 **Status**: In development — consolidating core features and optimizing workflow  
> 🧠 **Mindset**: Clean code, modular systems, and future-proof design  
> 👥 **Goal**: Solo-friendly for now, but collaboration-ready down the road  

---

## 🚀 What This Repo Contains

- The **DiggerManager**: the command center for terrain generation
- A **voxel grid system** with support for sparse SDF data
- Mesh generation via **Marching Cubes**
- Blueprint-accessible brushes, including:
  - Add / remove terrain
  - Light spawning
  - Hole carving with shape libraries
- **Socket.IO Multiplayer System** *(early integration phase)*

---

## 📒 Notes for Future Me (or You, Collaborator!)

- This README is a **placeholder** — I’ll expand it once the core logic and UI stabilizes
- If you're jumping in to help later:
  - Check `DiggerManager.cpp` for key entry points
  - Review `BrushStroke` logic for sculpting interactions
  - Island-related logic is being rewritten — start from `main` branch
- Don’t panic if some branches look chaotic — cleanup in progress 🚿

---

## 🔥 Kick Patowski Coding Rules

- Always leave code cleaner than you found it 🧽
- Function names should explain themselves — like good signposts
- Modularity = maintainability
- If it’s broken, fix it loud (log everything first)

---

## 🧱 TODO (Quick Overview)

- [x] Consolidate branches into clean `main`
- [x] Archive or extract legacy landscape materials
- [ ] Finalize brush system with undo stack
- [ ] Modularize mesh extraction
- [ ] Complete light spawning + save/load system
- [ ] Finalize multiplayer brush replication
- [ ] Write proper technical docs for collaborators

---

## 🧠 Credits & Shoutouts

- Developed in Unreal Engine 5.2+
- Uses [Getnamo's Socket.IO Plugin](https://github.com/getnamo/socketio-client-ue4) for Node integration
- Inspired by countless late nights, supportive family, and little feet pattering behind the desk 💙

---

> _“It doesn’t have to be perfect — it just has to dig.”_

---

