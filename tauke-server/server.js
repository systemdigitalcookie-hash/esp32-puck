// Puck <-> Grok Tauke voice bridge. No dependencies (Node >= 20: fetch, FormData, Blob).
// POST /talk  body: raw 16 kHz mono s16le PCM (Content-Type audio/pcm or application/octet-stream) or a WAV file.
//   headers: Authorization: Bearer <DEVICE_TOKEN>, X-Device-Id: <any id, e.g. MAC>
//   200 -> body: 16 kHz mono s16le PCM (Content-Type audio/pcm, Content-Length set)
//          headers X-Transcript, X-Reply (percent-encoded UTF-8)
//   errors -> JSON {error}, with X-Error header (short, ASCII) for the device screen.
// Env: XAI_API_KEY, DEVICE_TOKEN (required); CHAT_MODEL, STT_MODEL, TTS_VOICE (optional).
const http = require('http');
const crypto = require('crypto');

const PORT = process.env.PORT || 8080;
const XAI = process.env.XAI_BASE || 'https://api.x.ai/v1';
const KEY = process.env.XAI_API_KEY;
const TOKEN = process.env.DEVICE_TOKEN;
const CHAT_MODEL = process.env.CHAT_MODEL || 'grok-4.20-0309-non-reasoning';
const STT_MODEL = process.env.STT_MODEL || 'grok-voice-transcribe-2.0';
const TTS_VOICE = process.env.TTS_VOICE || 'rex';
const SR = 16000;
const MAX_BODY = 2 * 1024 * 1024;   // ~60 s of 16 kHz PCM
const HIST_TURNS = 8;               // messages kept per device (user+assistant)
const HIST_TTL = 30 * 60 * 1000;

const PERSONA = `You are Grok Tauke, Thomas Yang's friendly business-partner assistant at Digital Cookie, a Notion and Grok Bot consultancy in Malaysia.
You are speaking out loud through a small round desk gadget called the puck, so:
- Reply in 1 to 3 short spoken sentences. No lists, no markdown, no emoji, no URLs.
- Warm, practical, a bit cheeky, Malaysian-English friendly (a light "lah" or "boss" now and then is fine, don't overdo it).
- If you don't know something specific about Thomas's business, say so briefly and suggest a next step.
- If the transcript is empty or garbled, ask him to say it again.`;

const history = new Map(); // deviceId -> {msgs: [], t}
// Abuse limits (the device token ships inside public firmware, lightly obfuscated)
const ALLOWED = (process.env.ALLOWED_DEVICES || '').split(',').map((s) => s.trim().toUpperCase()).filter(Boolean);
const DEV_DAILY = +(process.env.DEVICE_DAILY_LIMIT || 150);
const ALL_DAILY = +(process.env.GLOBAL_DAILY_LIMIT || 400);
const usage = { day: '', all: 0, dev: new Map() };
function allowUse(dev) {
  const d = new Date().toISOString().slice(0, 10);
  if (usage.day !== d) { usage.day = d; usage.all = 0; usage.dev.clear(); }
  const n = usage.dev.get(dev) || 0;
  if (n >= DEV_DAILY || usage.all >= ALL_DAILY) return false;
  usage.dev.set(dev, n + 1); usage.all++;
  return true;
}

function wav(pcm) {
  const h = Buffer.alloc(44);
  h.write('RIFF', 0); h.writeUInt32LE(36 + pcm.length, 4); h.write('WAVEfmt ', 8);
  h.writeUInt32LE(16, 16); h.writeUInt16LE(1, 20); h.writeUInt16LE(1, 22);
  h.writeUInt32LE(SR, 24); h.writeUInt32LE(SR * 2, 28); h.writeUInt16LE(2, 32); h.writeUInt16LE(16, 34);
  h.write('data', 36); h.writeUInt32LE(pcm.length, 40);
  return Buffer.concat([h, pcm]);
}

class HttpErr extends Error { constructor(status, short, detail) { super(detail || short); this.status = status; this.short = short; } }

async function xaiCheck(r, what) {
  if (r.ok) return;
  const body = (await r.text()).slice(0, 300);
  let short = `${what} error ${r.status}`;
  if (r.status === 402 || /credits|spending limit/i.test(body)) short = 'xAI credits empty';
  else if (r.status === 401) short = 'xAI key invalid';
  else if (r.status === 429) short = 'xAI rate limited';
  console.error(`[${what}] ${r.status} ${body}`);
  throw new HttpErr(502, short, `${what}: ${r.status}`);
}

async function stt(wavBuf) {
  const fd = new FormData();
  fd.append('model', STT_MODEL);
  fd.append('file', new Blob([wavBuf], { type: 'audio/wav' }), 'speech.wav');
  const r = await fetch(`${XAI}/stt`, { method: 'POST', headers: { Authorization: `Bearer ${KEY}` }, body: fd, signal: AbortSignal.timeout(30000) });
  await xaiCheck(r, 'stt');
  const j = await r.json();
  return (j.text || '').trim();
}

async function chat(deviceId, text) {
  const now = Date.now();
  let h = history.get(deviceId);
  if (!h || now - h.t > HIST_TTL) h = { msgs: [], t: now };
  const messages = [{ role: 'system', content: PERSONA }, ...h.msgs, { role: 'user', content: text }];
  const r = await fetch(`${XAI}/chat/completions`, {
    method: 'POST', headers: { Authorization: `Bearer ${KEY}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ model: CHAT_MODEL, messages, max_tokens: 160, temperature: 0.7 }),
    signal: AbortSignal.timeout(30000),
  });
  await xaiCheck(r, 'chat');
  const j = await r.json();
  let reply = (j.choices?.[0]?.message?.content || '').replace(/[*_#`>]/g, '').trim() || 'Sorry boss, I blanked out. Say again?';
  h.msgs.push({ role: 'user', content: text }, { role: 'assistant', content: reply });
  h.msgs = h.msgs.slice(-HIST_TURNS); h.t = now;
  history.set(deviceId, h);
  return reply;
}

async function tts(text) {
  const r = await fetch(`${XAI}/tts`, {
    method: 'POST', headers: { Authorization: `Bearer ${KEY}`, 'Content-Type': 'application/json' },
    body: JSON.stringify({ text, voice_id: TTS_VOICE, language: 'en', output_format: { codec: 'pcm', sample_rate: SR } }),
    signal: AbortSignal.timeout(30000),
  });
  await xaiCheck(r, 'tts');
  let pcm = Buffer.from(await r.arrayBuffer());
  if (pcm.length > 44 && pcm.toString('ascii', 0, 4) === 'RIFF') pcm = pcm.subarray(44); // in case a WAV comes back
  return pcm.subarray(0, pcm.length & ~1);
}

function tokenOk(req) {
  const a = req.headers['authorization'] || '';
  const got = Buffer.from(a.startsWith('Bearer ') ? a.slice(7) : (req.headers['x-device-token'] || ''));
  const want = Buffer.from(TOKEN || '');
  return want.length > 0 && got.length === want.length && crypto.timingSafeEqual(got, want);
}

function sendErr(res, status, short) {
  const b = JSON.stringify({ error: short });
  res.writeHead(status, { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(b), 'X-Error': short.replace(/[^\x20-\x7e]/g, '') });
  res.end(b);
}

const enc = (s) => encodeURIComponent(s);

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://x');
  if (req.method === 'GET' && (url.pathname === '/' || url.pathname === '/health')) {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    return res.end(JSON.stringify({ ok: true, service: 'puck-tauke', chat: CHAT_MODEL, stt: STT_MODEL, voice: TTS_VOICE }));
  }
  if (url.pathname !== '/talk') return sendErr(res, 404, 'not found');
  if (req.method !== 'POST') return sendErr(res, 405, 'POST only');
  if (!tokenOk(req)) return sendErr(res, 401, 'bad device token');
  if (!KEY) return sendErr(res, 500, 'server has no XAI key');

  const dev = String(req.headers['x-device-id'] || 'default').slice(0, 64).toUpperCase();
  if (ALLOWED.length && !ALLOWED.includes(dev)) { console.warn(`[talk] rejected device ${dev}`); return sendErr(res, 403, 'device not allowed'); }
  if (!allowUse(dev)) return sendErr(res, 429, 'daily limit reached');
  const t0 = Date.now();
  const chunks = []; let n = 0;
  try {
    for await (const c of req) { n += c.length; if (n > MAX_BODY) throw new HttpErr(413, 'audio too long'); chunks.push(c); }
    let audio = Buffer.concat(chunks);
    const isWav = audio.length > 44 && audio.toString('ascii', 0, 4) === 'RIFF';
    const pcmLen = isWav ? audio.length - 44 : audio.length;
    if (pcmLen < SR * 2 * 0.3) throw new HttpErr(400, 'audio too short');
    const wavBuf = isWav ? audio : wav(audio.subarray(0, audio.length & ~1));

    const text = await stt(wavBuf);
    const t1 = Date.now();
    const reply = text ? await chat(dev, text) : "Sorry boss, I didn't catch that. Try again?";
    const t2 = Date.now();
    const pcm = await tts(reply);
    const t3 = Date.now();
    console.log(`[talk] dev=${dev} in=${(pcmLen / 32000).toFixed(1)}s stt=${t1 - t0}ms chat=${t2 - t1}ms tts=${t3 - t2}ms out=${(pcm.length / 32000).toFixed(1)}s | "${text}" -> "${reply}"`);
    res.writeHead(200, {
      'Content-Type': 'audio/pcm', 'Content-Length': pcm.length,
      'X-Transcript': enc(text).slice(0, 1500), 'X-Reply': enc(reply).slice(0, 3000),
      'X-Sample-Rate': String(SR), 'X-Timing': `${t1 - t0},${t2 - t1},${t3 - t2}`,
    });
    res.end(pcm);
  } catch (e) {
    const status = e.status || (e.name === 'TimeoutError' ? 504 : 500);
    const short = e.short || (e.name === 'TimeoutError' ? 'xAI timeout' : 'server error');
    if (!e.status) console.error('[talk]', e);
    if (!res.headersSent) sendErr(res, status, short); else res.end();
  }
});
server.requestTimeout = 120000;
server.listen(PORT, () => console.log(`puck-tauke on :${PORT} chat=${CHAT_MODEL} stt=${STT_MODEL} voice=${TTS_VOICE} token=${TOKEN ? 'set' : 'MISSING'}`));
