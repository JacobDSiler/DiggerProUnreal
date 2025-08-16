# DiggerConnect Lobby (Alpha)

## Dev
- cp .env.example .env
- npm install
- npm run dev
- Health: http://localhost:3000/health
- Test client: open test-client/index.html in two tabs (or use Unreal).

## Cloudflare Tunnel (alpha)
- cloudflared login
- cloudflared tunnel --url http://localhost:3000
- Use printed HTTPS URL in Unreal plugin settings.

## Render (beta)
- Connect repo or set root to this subfolder
- Build: npm install
- Start: npm start
- Env: ALLOWED_ORIGIN=https://YOUR-SITE,https://ANOTHER-SITE
