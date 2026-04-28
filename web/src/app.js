import { SympleClient, Symple } from 'symple-client'
import { CallManager } from 'symple-player'

// ---------------------------------------------------------------------------
// DOM
// ---------------------------------------------------------------------------

const $root = document.documentElement
const $stage = document.getElementById('stage')
const $status = document.getElementById('status')
const $peerList = document.getElementById('peer-list')
const $peerCount = document.getElementById('peer-count')
const $remoteVideo = document.getElementById('remote-video')
const $localVideo = document.getElementById('local-video')
const $localPreview = document.getElementById('local-preview')
const $playerOverlay = document.getElementById('player-overlay')
const $controls = document.getElementById('controls')
const $rail = document.getElementById('rail')
const $railToggle = document.getElementById('rail-toggle')
const $liveBar = document.getElementById('live-bar')

const $sourceLabel = document.getElementById('source-label')
const $railSource = document.getElementById('rail-source')
const $railVersion = document.getElementById('rail-version')
const $connInfo = document.getElementById('connection-info')

const $metricLatency = document.getElementById('metric-latency')
const $latencyValue = document.getElementById('latency-value')
const $fpsValue = document.getElementById('fps-value')
const $codecValue = document.getElementById('codec-value')
const $bitrateValue = document.getElementById('bitrate-value')

const $voice = document.getElementById('voice')
const $voiceBar = document.getElementById('voice-bar')
const $voicePeak = document.getElementById('voice-peak')

const $visionCanvas = document.getElementById('vision-overlay')
const $eventList = document.getElementById('event-list')
const $eventStatus = document.getElementById('event-status')
const $intelligenceSourceFps = document.getElementById('intelligence-source-fps')
const $intelligenceSampledFps = document.getElementById('intelligence-sampled-fps')
const $intelligenceVisionQueue = document.getElementById('intelligence-vision-queue')
const $intelligenceVisionDropped = document.getElementById('intelligence-vision-dropped')
const $intelligenceLatency = document.getElementById('intelligence-latency')
const $intelligenceArtifacts = document.getElementById('intelligence-artifacts')

const $btnMute = document.getElementById('btn-mute')
const $btnCamera = document.getElementById('btn-camera')
const $btnSnapshot = document.getElementById('btn-snapshot')
const $btnFullscreen = document.getElementById('btn-fullscreen')
const $btnHangup = document.getElementById('btn-hangup')

const $latencySpark = document.getElementById('latency-spark')
const $waveform = document.getElementById('waveform')
const $snapshotFlash = document.getElementById('snapshot-flash')

const RAIL_PREF_KEY = 'icey:rail'
const MAX_EVENTS = 12

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

let client = null
let calls = null
let user = ''
let runtimeConfig = null
let statsInterval = null

const callState = {
  // Browser-side speaker (the <video> element's audio output). Default-muted
  // on a single-machine demo because the FaceTime mic captured by ffmpeg will
  // pick up the speakers and create a feedback loop. Unmute when on
  // headphones or a separate device.
  speakerMuted: true,
  videoMuted: false
}

const frameTracker = {
  lastFrameTs: 0,
  framesSinceTick: 0,
  fpsEma: 0,
  latencyEma: 0,
  videoClockRate: 90000,
  rvfcHandle: 0,
  fpsInterval: 0,
  stallInterval: 0,
  // Rolling ring buffer of latency samples for the sparkline. One sample
  // per fpsInterval tick (every 500ms), so 60 samples = 30 seconds.
  latencyHistory: new Array(60).fill(null),
  latencyHistoryIdx: 0
}

const audioMonitor = {
  ctx: null,
  analyser: null,
  buffer: null,
  rafId: 0,
  cloneStream: null
}

const voiceState = {
  decayTimer: 0,
  peakTimer: 0
}

const visionState = {
  regions: [],
  // Most recent motion grid (set of normalized 0..1 cell diffs) for the
  // background heatmap. The grid is replaced on each event; nothing
  // accumulates across events.
  grid: null,
  rafId: 0
}

window.__mediaServerState = {
  get client () { return client },
  get calls () { return calls },
  get runtimeConfig () { return runtimeConfig }
}

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

function getWsUrl () {
  const proto = location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${proto}//${location.host}`
}

async function fetchRuntimeConfig () {
  const response = await fetch('/api/config')
  if (!response.ok) throw new Error(`Failed to load runtime config: ${response.status}`)
  const config = await response.json()
  if (!config || config.status !== 'ok') throw new Error('Invalid runtime config payload')
  return config
}

function buildIceServers (config) {
  const servers = []
  const turn = config?.turn
  if (turn?.enabled) {
    const host = turn.host && turn.host !== '0.0.0.0' && turn.host !== '::'
      ? turn.host
      : location.hostname
    const port = Number(turn.port) || 3478
    const username = turn.username || 'icey'
    const credential = turn.credential || 'icey'
    servers.push({ urls: `turn:${host}:${port}?transport=udp`, username, credential })
    servers.push({ urls: `turn:${host}:${port}?transport=tcp`, username, credential })
  }
  const stunUrls = Array.isArray(config?.stun?.urls) ? config.stun.urls : []
  for (const url of stunUrls) servers.push({ urls: url })
  return servers
}

function applyRuntimeIdentity () {
  const src = runtimeConfig?.source?.value
    || runtimeConfig?.stream?.source
    || ''
  const mode = runtimeConfig?.mode || ''
  $sourceLabel.textContent = src ? src : (mode ? `mode: ${mode}` : '—')
  $railSource.textContent = src || '(no source configured)'
  if (runtimeConfig?.version) $railVersion.textContent = `v${runtimeConfig.version}`

  const parts = []
  if (user) parts.push(user)
  if (runtimeConfig?.service) parts.push(runtimeConfig.service)
  if (runtimeConfig?.mode) parts.push(runtimeConfig.mode)
  $connInfo.textContent = parts.length > 0 ? parts.join(' · ') : '—'

  const productName = runtimeConfig?.product || 'icey'
  document.title = runtimeConfig?.mode
    ? `${productName} · ${runtimeConfig.mode}`
    : productName
}

async function connect () {
  const url = getWsUrl()
  setStatus('connecting')

  runtimeConfig = await fetchRuntimeConfig()
  user = 'viewer-' + Math.random().toString(36).slice(2, 6)
  applyRuntimeIdentity()

  client = new SympleClient({
    url,
    token: '',
    peer: { user, name: 'Browser Viewer', type: 'viewer' }
  })

  client.on('connect', () => {
    setStatus('online')
    applyRuntimeIdentity()
    updatePeerList()
  })

  client.on('presence',  () => updatePeerList())
  client.on('addPeer',   () => updatePeerList())
  client.on('removePeer', () => updatePeerList())

  client.on('disconnect', () => {
    setStatus('offline')
    $peerList.innerHTML = '<li class="peer-empty">No peers online</li>'
    setPeerCount(0)
    // If a call was active, force-tear-down its UI state regardless of
    // whether the call manager fires 'ended' on its own.
    resetSessionState({ reason: 'disconnect' })
  })

  client.on('error', (err) => console.error('Client error:', err))
  client.on('message', (message) => handleSympleMessage(message))

  calls = new CallManager(client, $remoteVideo, {
    rtcConfig: { iceServers: buildIceServers(runtimeConfig) },
    mediaConstraints: { audio: true, video: true },
    localMedia: false
  })

  calls.on('incoming', (peerId) => {
    console.log('Incoming call from', peerId)
    calls.accept()
  })

  calls.on('active', (peerId) => {
    console.log('Call active with', peerId)
    showControls(true)
    $stage.dataset.empty = 'false'
    resetIntelligenceFeed('listening')
    startStats()
    startFrameTracker()
    // startAudioWaveform() is intentionally not called: the Chrome bug
    // around MediaStreamAudioSourceNode silencing the <video> element's
    // playback can still surface even with a cloned track. Keep the
    // function in place behind the scenes; re-enable once we have a
    // verified workaround (likely an HTMLAudioElement-based path).
    syncMuteButtons()
    updatePeerList()
  })

  calls.on('localstream', (stream) => {
    $localVideo.srcObject = stream
    $localPreview.classList.remove('is-hidden')
  })

  calls.on('ended', (_peerId, reason) => {
    console.log('Call ended:', reason)
    resetSessionState({ reason: 'ended' })
  })

  calls.on('error', (err) => console.error('Call error:', err))

  client.connect()
}

// ---------------------------------------------------------------------------
// Session-state reset (single source of truth)
// ---------------------------------------------------------------------------

function resetSessionState (_opts = {}) {
  showControls(false)
  $stage.dataset.empty = 'true'

  if ($remoteVideo) $remoteVideo.srcObject = null
  if ($localVideo) {
    const stream = $localVideo.srcObject
    if (stream) {
      try { stream.getTracks().forEach((t) => t.stop()) } catch (_) {}
    }
    $localVideo.srcObject = null
  }
  $localPreview.classList.add('is-hidden')

  callState.speakerMuted = true
  callState.videoMuted = false
  syncMuteButtons()

  if (document.fullscreenElement) {
    document.exitFullscreen().catch(() => {})
  }

  resetIntelligenceFeed('waiting')
  stopStats()
  stopFrameTracker()
  stopAudioWaveform()
  resetVisionRegions()
  resetVoice()
  setLatency(null)
  setFps(null)
  setCodec(null)
  setBitrate(null)

  updatePeerList()
}

function syncMuteButtons () {
  $btnMute.dataset.muted = callState.speakerMuted ? 'true' : 'false'
  $btnMute.querySelector('.btn-ctl__glyph').textContent = callState.speakerMuted ? '🔇' : '🔊'
  if ($remoteVideo) $remoteVideo.muted = !!callState.speakerMuted
  $btnCamera.dataset.muted = callState.videoMuted ? 'true' : 'false'
  $btnCamera.style.opacity = callState.videoMuted ? '0.55' : '1'
}

// ---------------------------------------------------------------------------
// Peer list
// ---------------------------------------------------------------------------

function updatePeerList () {
  if (!client || !client.roster) return

  $peerList.innerHTML = ''
  const peers = client.roster.data.filter((p) => p.id !== client.peer?.id)
  setPeerCount(peers.length)

  if (peers.length === 0) {
    $peerList.innerHTML = '<li class="peer-empty">No peers online</li>'
    return
  }

  for (const peer of peers) {
    const li = document.createElement('li')
    li.className = 'peer-item'

    const info = document.createElement('div')
    info.className = 'peer-info'

    const dot = document.createElement('span')
    dot.className = 'peer-dot'

    const nameEl = document.createElement('div')
    const name = document.createElement('span')
    name.className = 'peer-name'
    name.textContent = peer.name || peer.user || peer.id
    nameEl.appendChild(name)

    if (peer.type) {
      const type = document.createElement('span')
      type.className = 'peer-type'
      type.textContent = peer.type
      nameEl.appendChild(type)
    }

    info.appendChild(dot)
    info.appendChild(nameEl)

    const address = Symple.buildAddress(peer)
    const inCall = !!(calls && calls.remotePeerId === address)
    if (inCall) li.classList.add('is-active')

    const actionBar = document.createElement('div')
    actionBar.className = 'peer-actions'

    if (inCall) {
      const hangup = document.createElement('button')
      hangup.textContent = 'Hangup'
      hangup.className = 'hangup'
      hangup.onclick = (ev) => { ev.stopPropagation(); calls.hangup() }
      actionBar.appendChild(hangup)
      li.classList.add('is-clickable')
      li.onclick = () => calls.hangup()
    } else {
      const actions = getPeerActions(peer)
      for (const action of actions) {
        const btn = document.createElement('button')
        btn.textContent = action.label
        btn.onclick = (ev) => { ev.stopPropagation(); placeCall(address, action.options) }
        actionBar.appendChild(btn)
      }
      // The whole row triggers the primary (first) action so misclicks on the
      // name or empty space still do the obvious thing. Buttons keep their own
      // handlers and stop propagation so they remain individually clickable.
      const primary = actions[0]
      if (primary) {
        li.classList.add('is-clickable')
        li.onclick = () => placeCall(address, primary.options)
      }
    }

    li.appendChild(info)
    li.appendChild(actionBar)
    $peerList.appendChild(li)
  }
}

function setPeerCount (n) {
  if (!$peerCount) return
  $peerCount.textContent = String(n)
}

function getPeerActions (peer) {
  const capabilities = Array.isArray(peer.capabilities) ? peer.capabilities : []
  const mode = typeof peer.mode === 'string' ? peer.mode : ''
  // Enable WebRTC's built-in echo cancellation, noise suppression, and AGC
  // for browser-captured audio. This handles the case where the browser's
  // mic picks up its own speaker output. (It does NOT fix the FaceTime →
  // ffmpeg path on this demo box — that is a server-side capture loop.)
  const cleanAudio = {
    echoCancellation: true,
    noiseSuppression: true,
    autoGainControl: true
  }
  const publishConstraints = mode === 'record'
    ? { audio: false, video: true }
    : { audio: cleanAudio, video: true }
  const watchConstraints = { audio: cleanAudio, video: true }

  if (capabilities.includes('publish') && capabilities.includes('view')) {
    return [
      { label: 'Broadcast', options: { localMedia: true, receiveMedia: false, mediaConstraints: publishConstraints } },
      { label: 'Watch', options: { localMedia: false, mediaConstraints: watchConstraints } }
    ]
  }
  if (capabilities.includes('publish')) {
    return [{ label: 'Broadcast', options: { localMedia: true, receiveMedia: false, mediaConstraints: publishConstraints } }]
  }
  if (capabilities.includes('view')) {
    return [{ label: 'Watch', options: { localMedia: false, mediaConstraints: watchConstraints } }]
  }
  return [{ label: 'Call', options: { localMedia: true, receiveMedia: true, mediaConstraints: watchConstraints } }]
}

function placeCall (address, options) {
  console.debug('[icey] placeCall', address, 'calls?', !!calls,
    'state:', calls?.callState, 'remote:', calls?.remotePeerId)
  if (!calls) {
    console.warn('[icey] placeCall: calls not initialised')
    return
  }
  if (calls.remotePeerId && calls.remotePeerId !== address) {
    try { calls.hangup() } catch (e) { console.warn('[icey] hangup failed:', e) }
  }
  try {
    calls.call(address, options)
    console.debug('[icey] calls.call returned ok')
  } catch (e) {
    console.error('[icey] calls.call threw:', e)
  }
}

// ---------------------------------------------------------------------------
// Symple intelligence messages
// ---------------------------------------------------------------------------

function handleSympleMessage (message) {
  const subtype = message?.subtype
  if (subtype === 'icey:vision' || subtype === 'icey:speech') {
    console.debug('[icey] intelligence message', subtype, message)
  }
  if (subtype === 'icey:vision') {
    handleVisionEvent(message?.data || {})
    appendEventListEntry('vision', message?.data || {})
    return
  }
  if (subtype === 'icey:speech') {
    handleSpeechEvent(message?.data || {})
    appendEventListEntry('speech', message?.data || {})
  }
}

function appendEventListEntry (kind, event) {
  if (!$eventList) return

  const empty = $eventList.querySelector('.event-empty')
  if (empty) empty.remove()
  if ($eventStatus) $eventStatus.textContent = `${kind} · live`

  const item = document.createElement('li')
  item.className = `event-item ${kind}`

  const meta = document.createElement('div')
  meta.className = 'event-meta'

  const label = document.createElement('span')
  label.className = 'event-label'
  label.textContent = kind

  const time = document.createElement('span')
  time.textContent = formatEventTime(event)
  meta.appendChild(label)
  meta.appendChild(time)

  const summary = document.createElement('div')
  summary.className = 'event-summary'
  summary.textContent = describeEvent(kind, event)

  item.appendChild(meta)
  item.appendChild(summary)

  const links = buildArtifactLinks(event)
  if (links.length > 0) {
    const linksEl = document.createElement('div')
    linksEl.className = 'event-links'
    for (const a of links) {
      const link = document.createElement('a')
      link.href = a.url
      link.textContent = a.label
      link.target = '_blank'
      link.rel = 'noreferrer'
      linksEl.appendChild(link)
    }
    item.appendChild(linksEl)
  }

  $eventList.prepend(item)
  while ($eventList.children.length > MAX_EVENTS) {
    $eventList.removeChild($eventList.lastElementChild)
  }
}

function buildArtifactLinks (event) {
  const links = []
  const snapshot = event?.data?.snapshot?.url
  if (typeof snapshot === 'string' && snapshot.length > 0) links.push({ label: 'snapshot', url: snapshot })
  const clip = event?.data?.clip?.url
  if (typeof clip === 'string' && clip.length > 0) links.push({ label: 'clip', url: clip })
  return links
}

function formatEventTime (event) {
  const usec = Number(
    event?.frame?.ptsUsec ?? event?.audio?.timeUsec ?? event?.time ??
    event?.audio?.time ?? event?.frame?.time ?? event?.emittedAtUsec ?? 0
  )
  if (!Number.isFinite(usec) || usec <= 0) return 'live'
  return `${(usec / 1000000).toFixed(2)}s`
}

function describeEvent (kind, event) {
  if (kind === 'vision') {
    const detection = Array.isArray(event?.detections) ? event.detections[0] : null
    const label = detection?.label || event?.detector || 'detection'
    const score = Number(detection?.confidence ?? event?.data?.score ?? 0)
    return score > 0 ? `${label} · ${(score * 100).toFixed(1)}%` : label
  }
  const level = Number(event?.level ?? 0)
  const state = typeof event?.type === 'string' ? event.type.replace('speech:', '') : 'event'
  return `${state} · level ${(level * 100).toFixed(0)}%`
}

function resetIntelligenceFeed (statusText) {
  if ($eventStatus) $eventStatus.textContent = statusText
  if ($eventList) $eventList.innerHTML = '<li class="event-empty">No events yet</li>'
  updateIntelligenceStats(null)
}

// ---------------------------------------------------------------------------
// Voice activity
// ---------------------------------------------------------------------------

function handleSpeechEvent (event) {
  const level = clamp(Number(event?.level ?? 0), 0, 1)
  const peak = clamp(Number(event?.peak ?? 0), 0, 1)
  const active = !!event?.active

  $voice.dataset.active = active ? 'true' : 'false'
  $voiceBar.style.width = `${level * 100}%`
  $voicePeak.style.left = `${peak * 100}%`

  clearTimeout(voiceState.decayTimer)
  voiceState.decayTimer = setTimeout(() => {
    $voice.dataset.active = 'false'
    $voiceBar.style.width = '0%'
  }, 600)

  clearTimeout(voiceState.peakTimer)
  voiceState.peakTimer = setTimeout(() => {
    $voicePeak.style.left = '0%'
  }, 1500)
}

function resetVoice () {
  clearTimeout(voiceState.decayTimer)
  clearTimeout(voiceState.peakTimer)
  $voice.dataset.active = 'false'
  $voiceBar.style.width = '0%'
  $voicePeak.style.left = '0%'
}

// ---------------------------------------------------------------------------
// Motion regions overlay (canvas)
// ---------------------------------------------------------------------------

function handleVisionEvent (event) {
  const detections = Array.isArray(event?.detections) ? event.detections : []
  const now = performance.now()
  const ttl = 600

  for (const det of detections) {
    // Region coords are already normalized 0..1 on the icey side. Do not
    // divide by frame dimensions; that was a long-standing bug that
    // collapsed every box to a single pixel near (0, 0).
    const region = det?.region || det
    const x = Number(region?.x ?? 0)
    const y = Number(region?.y ?? 0)
    const w = Number(region?.width ?? region?.w ?? 0)
    const h = Number(region?.height ?? region?.h ?? 0)
    if (!Number.isFinite(x) || !Number.isFinite(y) || w <= 0 || h <= 0) continue

    visionState.regions.push({
      x, y, w, h,
      label: det?.label || event?.detector || '',
      conf: Number(det?.confidence ?? event?.data?.score ?? 0),
      bornAt: now,
      expiresAt: now + ttl
    })
  }

  if (visionState.regions.length > 64) {
    visionState.regions.splice(0, visionState.regions.length - 64)
  }

  // Per-cell normalized diff grid for the background heatmap. Replaced on
  // each event; the grid itself fades over the same ttl as the bounding
  // boxes via drawVisionRegions.
  const grid = event?.data?.grid
  if (grid && Array.isArray(grid.cells) && grid.width > 0 && grid.height > 0) {
    visionState.grid = {
      width: Number(grid.width),
      height: Number(grid.height),
      cells: grid.cells,
      bornAt: now,
      expiresAt: now + ttl
    }
  }

  ensureVisionLoop()
}

function resetVisionRegions () {
  visionState.regions.length = 0
  visionState.grid = null
  if (visionState.rafId) {
    cancelAnimationFrame(visionState.rafId)
    visionState.rafId = 0
  }
  if (!$visionCanvas) return
  const ctx = $visionCanvas.getContext('2d')
  if (ctx) ctx.clearRect(0, 0, $visionCanvas.width, $visionCanvas.height)
}

function ensureVisionLoop () {
  if (visionState.rafId) return
  const tick = () => {
    drawVisionRegions()
    const more = visionState.regions.length > 0 || !!visionState.grid
    if (more) {
      visionState.rafId = requestAnimationFrame(tick)
    } else {
      visionState.rafId = 0
    }
  }
  visionState.rafId = requestAnimationFrame(tick)
}

function drawVisionRegions () {
  const canvas = $visionCanvas
  const video = $remoteVideo
  if (!canvas || !video) return

  const rect = video.getBoundingClientRect()
  const dpr = window.devicePixelRatio || 1
  const targetW = Math.round(rect.width * dpr)
  const targetH = Math.round(rect.height * dpr)
  if (canvas.width !== targetW) canvas.width = targetW
  if (canvas.height !== targetH) canvas.height = targetH

  const ctx = canvas.getContext('2d')
  ctx.clearRect(0, 0, canvas.width, canvas.height)

  const videoW = video.videoWidth
  const videoH = video.videoHeight
  if (!videoW || !videoH) return

  const elementAspect = rect.width / rect.height
  const videoAspect = videoW / videoH
  let drawW, drawH, drawX, drawY
  if (videoAspect > elementAspect) {
    drawW = rect.width
    drawH = rect.width / videoAspect
    drawX = 0
    drawY = (rect.height - drawH) / 2
  } else {
    drawH = rect.height
    drawW = rect.height * videoAspect
    drawX = (rect.width - drawW) / 2
    drawY = 0
  }
  drawW *= dpr; drawH *= dpr; drawX *= dpr; drawY *= dpr

  const now = performance.now()

  // Background heatmap from the latest grid. Draws under the bounding
  // boxes so the boxes still pop. Cells fade in opacity by their normalized
  // diff value, then the whole grid fades by age.
  const grid = visionState.grid
  if (grid && now <= grid.expiresAt) {
    const gridAge = clamp((now - grid.bornAt) / (grid.expiresAt - grid.bornAt), 0, 1)
    const gridAlpha = 1 - gridAge
    const cellW = drawW / grid.width
    const cellH = drawH / grid.height
    for (let gy = 0; gy < grid.height; gy++) {
      for (let gx = 0; gx < grid.width; gx++) {
        const v = Number(grid.cells[gy * grid.width + gx]) || 0
        if (v < 0.02) continue
        const intensity = clamp(v * 2, 0, 1) * 0.45 * gridAlpha
        if (intensity <= 0.01) continue
        ctx.fillStyle = `rgba(248, 113, 113, ${intensity})`
        ctx.fillRect(
          drawX + gx * cellW,
          drawY + gy * cellH,
          cellW + 0.5,
          cellH + 0.5)
      }
    }
  } else if (grid) {
    visionState.grid = null
  }

  const remaining = []
  for (const r of visionState.regions) {
    if (now > r.expiresAt) continue
    remaining.push(r)
    const ageRatio = clamp((now - r.bornAt) / (r.expiresAt - r.bornAt), 0, 1)
    const alpha = 1 - ageRatio

    const x = drawX + r.x * drawW
    const y = drawY + r.y * drawH
    const w = r.w * drawW
    const h = r.h * drawH

    ctx.strokeStyle = `rgba(74, 222, 128, ${0.85 * alpha})`
    ctx.lineWidth = 2 * dpr
    ctx.strokeRect(x, y, w, h)

    ctx.fillStyle = `rgba(74, 222, 128, ${0.08 * alpha})`
    ctx.fillRect(x, y, w, h)

    const tick = Math.min(12 * dpr, w / 4, h / 4)
    ctx.strokeStyle = `rgba(255, 255, 255, ${0.85 * alpha})`
    ctx.lineWidth = 1.5 * dpr
    ctx.beginPath()
    ctx.moveTo(x, y + tick); ctx.lineTo(x, y); ctx.lineTo(x + tick, y)
    ctx.moveTo(x + w - tick, y); ctx.lineTo(x + w, y); ctx.lineTo(x + w, y + tick)
    ctx.moveTo(x, y + h - tick); ctx.lineTo(x, y + h); ctx.lineTo(x + tick, y + h)
    ctx.moveTo(x + w - tick, y + h); ctx.lineTo(x + w, y + h); ctx.lineTo(x + w, y + h - tick)
    ctx.stroke()

    if (r.label) {
      ctx.font = `${11 * dpr}px ui-monospace, SFMono-Regular, Menlo, monospace`
      const text = r.conf > 0
        ? `${r.label} ${(r.conf * 100).toFixed(0)}%`
        : r.label
      const metrics = ctx.measureText(text)
      const padding = 4 * dpr
      const labelW = metrics.width + padding * 2
      const labelH = 16 * dpr
      ctx.fillStyle = `rgba(0, 0, 0, ${0.55 * alpha})`
      ctx.fillRect(x, y - labelH, labelW, labelH)
      ctx.fillStyle = `rgba(255, 255, 255, ${alpha})`
      ctx.fillText(text, x + padding, y - 4 * dpr)
    }
  }
  visionState.regions = remaining
}

// ---------------------------------------------------------------------------
// Frame callback — fps + live pulse + latency
// ---------------------------------------------------------------------------

function startFrameTracker () {
  stopFrameTracker()
  if (typeof $remoteVideo.requestVideoFrameCallback !== 'function') return

  frameTracker.lastFrameTs = 0
  frameTracker.framesSinceTick = 0
  frameTracker.fpsEma = 0
  frameTracker.latencyEma = 0

  const onFrame = (now, metadata) => {
    const ts = now
    const delta = ts - frameTracker.lastFrameTs
    frameTracker.lastFrameTs = ts
    frameTracker.framesSinceTick++

    if (delta > 0 && delta < 500) {
      const inst = 1000 / delta
      frameTracker.fpsEma = frameTracker.fpsEma > 0
        ? frameTracker.fpsEma * 0.85 + inst * 0.15
        : inst
    }

    // Glass-to-glass latency cannot be derived from rVFC alone in this
    // pipeline: ffmpeg's RTP muxer rebases timestamps to a stream-relative
    // origin, so metadata.rtpTimestamp does not encode capture wallclock.
    // metadata.captureTime is only populated when the sender sets the
    // abs-capture-time RTP extension, which icey doesn't yet. The buffer
    // metric is fed from RTCInboundRtpStreamStats.jitterBufferDelay instead;
    // see updateLatencyFromStats below.
    if (metadata?.captureTime) {
      const latencyMs = ts - metadata.captureTime
      if (Number.isFinite(latencyMs) && latencyMs >= 0 && latencyMs < 5000) {
        frameTracker.latencyEma = frameTracker.latencyEma > 0
          ? frameTracker.latencyEma * 0.85 + latencyMs * 0.15
          : latencyMs
        setLatency(frameTracker.latencyEma)
      }
    }

    pulseLiveBar()
    frameTracker.rvfcHandle = $remoteVideo.requestVideoFrameCallback(onFrame)
  }
  frameTracker.rvfcHandle = $remoteVideo.requestVideoFrameCallback(onFrame)

  frameTracker.fpsInterval = setInterval(() => {
    setFps(frameTracker.fpsEma > 0 ? frameTracker.fpsEma : null)
    pushLatencySample(frameTracker.latencyEma > 0 ? frameTracker.latencyEma : null)
    drawLatencySparkline()
  }, 500)

  frameTracker.stallInterval = setInterval(() => {
    const sinceLast = performance.now() - frameTracker.lastFrameTs
    if (sinceLast > 1000) $liveBar.classList.add('is-stalled')
    else $liveBar.classList.remove('is-stalled')
  }, 500)
}

function stopFrameTracker () {
  if (frameTracker.fpsInterval) { clearInterval(frameTracker.fpsInterval); frameTracker.fpsInterval = 0 }
  if (frameTracker.stallInterval) { clearInterval(frameTracker.stallInterval); frameTracker.stallInterval = 0 }
  $liveBar.classList.remove('is-stalled', 'is-pulsing')
  frameTracker.latencyHistory.fill(null)
  frameTracker.latencyHistoryIdx = 0
  drawLatencySparkline()
}

function pushLatencySample (ms) {
  frameTracker.latencyHistory[frameTracker.latencyHistoryIdx] = ms
  frameTracker.latencyHistoryIdx =
    (frameTracker.latencyHistoryIdx + 1) % frameTracker.latencyHistory.length
}

function drawLatencySparkline () {
  if (!$latencySpark) return
  const canvas = $latencySpark
  const dpr = window.devicePixelRatio || 1
  const w = canvas.width = Math.round(canvas.clientWidth * dpr)
  const h = canvas.height = Math.round(canvas.clientHeight * dpr)
  const ctx = canvas.getContext('2d')
  ctx.clearRect(0, 0, w, h)

  const buf = frameTracker.latencyHistory
  const n = buf.length
  // Read in oldest-first order from the ring buffer.
  const start = frameTracker.latencyHistoryIdx
  const samples = []
  for (let i = 0; i < n; i++) {
    const v = buf[(start + i) % n]
    samples.push(v)
  }

  // Vertical scale: 0..200ms, clipped above. Anything above the live
  // threshold (150ms) leans into amber; above 400ms is red. The live
  // colour matches the latency value text colour.
  const maxMs = 200
  let strokeColor = 'rgba(74, 222, 128, 0.85)'
  if ($metricLatency?.classList.contains('is-warn')) strokeColor = 'rgba(251, 191, 36, 0.85)'
  if ($metricLatency?.classList.contains('is-bad'))  strokeColor = 'rgba(248, 113, 113, 0.85)'

  ctx.strokeStyle = strokeColor
  ctx.lineWidth = Math.max(1, dpr)
  ctx.lineJoin = 'round'
  ctx.beginPath()
  let started = false
  for (let i = 0; i < n; i++) {
    const v = samples[i]
    if (v == null) { started = false; continue }
    const x = (i / (n - 1)) * w
    const y = h - (clamp(v, 0, maxMs) / maxMs) * h
    if (!started) { ctx.moveTo(x, y); started = true }
    else ctx.lineTo(x, y)
  }
  ctx.stroke()
}

let pulseTimeout = 0
function pulseLiveBar () {
  $liveBar.classList.add('is-pulsing')
  clearTimeout(pulseTimeout)
  pulseTimeout = setTimeout(() => $liveBar.classList.remove('is-pulsing'), 80)
}

// ---------------------------------------------------------------------------
// Remote audio waveform
//
// Tap the remote MediaStream into a Web Audio AnalyserNode and render a
// thin oscilloscope-style line at the bottom of the stage. This works even
// when the <video> element itself is muted, because muted only controls
// playback to speakers; the underlying track samples still flow through
// any AudioNode wired up to the same MediaStream.
// ---------------------------------------------------------------------------

function startAudioWaveform () {
  stopAudioWaveform()
  const stream = $remoteVideo?.srcObject
  if (!stream || !$waveform) return
  const tracks = typeof stream.getAudioTracks === 'function'
    ? stream.getAudioTracks()
    : []
  if (tracks.length === 0) return

  try {
    const Ctx = window.AudioContext || window.webkitAudioContext
    if (!Ctx) return

    // Chrome silences the <video> element's audio playback if you create
    // a MediaStreamAudioSourceNode from the same WebRTC remote stream
    // it's playing. Workaround: feed the AnalyserNode a CLONE of the
    // first audio track in its own dedicated MediaStream. The original
    // stream stays attached to <video> and keeps playing as normal.
    const cloneStream = new MediaStream([tracks[0].clone()])
    audioMonitor.cloneStream = cloneStream

    audioMonitor.ctx = new Ctx()
    audioMonitor.analyser = audioMonitor.ctx.createAnalyser()
    audioMonitor.analyser.fftSize = 512
    audioMonitor.analyser.smoothingTimeConstant = 0.6
    const src = audioMonitor.ctx.createMediaStreamSource(cloneStream)
    src.connect(audioMonitor.analyser)
    audioMonitor.buffer = new Uint8Array(audioMonitor.analyser.fftSize)

    const tick = () => {
      if (!audioMonitor.analyser) return
      drawWaveform()
      audioMonitor.rafId = requestAnimationFrame(tick)
    }
    audioMonitor.rafId = requestAnimationFrame(tick)
  }
  catch (e) {
    console.warn('[icey] audio waveform unavailable:', e)
    stopAudioWaveform()
  }
}

function stopAudioWaveform () {
  if (audioMonitor.rafId) {
    cancelAnimationFrame(audioMonitor.rafId)
    audioMonitor.rafId = 0
  }
  if (audioMonitor.ctx) {
    try { audioMonitor.ctx.close() } catch (_) {}
  }
  if (audioMonitor.cloneStream) {
    try {
      audioMonitor.cloneStream.getTracks().forEach((t) => t.stop())
    } catch (_) {}
  }
  audioMonitor.ctx = null
  audioMonitor.analyser = null
  audioMonitor.buffer = null
  audioMonitor.cloneStream = null
  if ($waveform) {
    const ctx = $waveform.getContext('2d')
    ctx.clearRect(0, 0, $waveform.width, $waveform.height)
  }
}

function drawWaveform () {
  if (!audioMonitor.analyser || !$waveform) return
  audioMonitor.analyser.getByteTimeDomainData(audioMonitor.buffer)

  const canvas = $waveform
  const dpr = window.devicePixelRatio || 1
  const w = canvas.width = Math.round(canvas.clientWidth * dpr)
  const h = canvas.height = Math.round(canvas.clientHeight * dpr)
  const ctx = canvas.getContext('2d')
  ctx.clearRect(0, 0, w, h)

  const buf = audioMonitor.buffer
  const n = buf.length
  ctx.lineWidth = Math.max(1, dpr)
  ctx.strokeStyle = 'rgba(147, 197, 253, 0.55)'
  ctx.beginPath()
  for (let i = 0; i < n; i++) {
    const x = (i / (n - 1)) * w
    // buf values are 0..255 with 128 as silence midline; map to -1..1.
    const v = (buf[i] - 128) / 128
    const y = h / 2 + v * (h / 2 - 1)
    if (i === 0) ctx.moveTo(x, y)
    else ctx.lineTo(x, y)
  }
  ctx.stroke()
}

// ---------------------------------------------------------------------------
// HUD setters
// ---------------------------------------------------------------------------

function setStatus (state) {
  if (!$status) return
  $status.classList.remove('status--online', 'status--offline', 'status--connecting')
  $status.classList.add(`status--${state}`)
  $status.textContent = state
}

function setLatency (ms) {
  if (!$latencyValue) return
  if (ms == null || !Number.isFinite(ms)) {
    $latencyValue.textContent = '—'
    $metricLatency?.classList.remove('is-warn', 'is-bad')
    return
  }
  const rounded = Math.round(ms)
  $latencyValue.textContent = String(rounded)
  // Threshold tuning is for jitter-buffer delay, not glass-to-glass.
  // Healthy WebRTC jitter buffers sit at 20-80ms on a stable network.
  $metricLatency.classList.toggle('is-warn', rounded >= 100 && rounded < 250)
  $metricLatency.classList.toggle('is-bad', rounded >= 250)
}

// Pull the average per-frame jitter-buffer delay from RTCInboundRtpStreamStats.
// This is the real, exact, no-side-channel measurement we can make from a
// browser polling the standard webrtc-stats API: how long each frame sits in
// the browser's de-jitter buffer between arriving and being played out.
// A subset of total latency, but an honest one.
function updateLatencyFromStats (stats) {
  let inbound = null
  for (const r of stats.values()) {
    if (r.type === 'inbound-rtp' && r.kind === 'video') {
      inbound = r
      break
    }
  }
  if (!inbound) return
  const total = Number(inbound.jitterBufferDelay)
  const count = Number(inbound.jitterBufferEmittedCount)
  if (!Number.isFinite(total) || !Number.isFinite(count) || count <= 0) return

  // jitterBufferDelay is cumulative seconds. Divide by frame count for the
  // session-average per-frame delay in ms.
  const avgMs = (total / count) * 1000
  if (!Number.isFinite(avgMs) || avgMs < 0 || avgMs > 5000) return

  frameTracker.latencyEma = frameTracker.latencyEma > 0
    ? frameTracker.latencyEma * 0.7 + avgMs * 0.3
    : avgMs
  setLatency(frameTracker.latencyEma)
}

function setFps (fps) {
  if (!$fpsValue) return
  $fpsValue.textContent = fps == null ? '—' : fps.toFixed(0)
}

function setCodec (label) {
  if (!$codecValue) return
  $codecValue.textContent = label || '—'
}

function setBitrate (label) {
  if (!$bitrateValue) return
  $bitrateValue.textContent = label || '—'
}

function showControls (visible) {
  $controls.classList.toggle('is-hidden', !visible)
}

// ---------------------------------------------------------------------------
// Stats polling (codec, bitrate, intelligence)
// ---------------------------------------------------------------------------

function startStats () {
  stopStats()
  statsInterval = setInterval(async () => {
    if (!calls?.player?.pc) return
    try {
      const statusResponse = await fetch('/api/status')
      if (statusResponse.ok) {
        const status = await statusResponse.json()
        updateIntelligenceStats(status?.intelligence || null)
      }

      const stats = await calls.player.pc.getStats()
      let codec = ''
      let dims = ''
      let bitrate = ''
      for (const report of stats.values()) {
        if (report.type === 'inbound-rtp' && report.kind === 'video') {
          if (report.decoderImplementation) codec = report.decoderImplementation
          if (report.frameWidth) dims = `${report.frameWidth}×${report.frameHeight}`
        }
        if (report.type === 'candidate-pair' && report.state === 'succeeded') {
          if (report.availableIncomingBitrate) {
            bitrate = `${(report.availableIncomingBitrate / 1e6).toFixed(1)} Mbps`
          }
        }
      }
      setCodec([codec, dims].filter(Boolean).join(' '))
      setBitrate(bitrate)
      updateLatencyFromStats(stats)
    } catch (_) { /* stats may fail during teardown */ }
  }, 1500)
}

function stopStats () {
  if (statsInterval) {
    clearInterval(statsInterval)
    statsInterval = null
  }
  setCodec(null)
  setBitrate(null)
  updateIntelligenceStats(null)
}

function updateIntelligenceStats (intelligence) {
  const vision = intelligence?.vision || null
  const formatRate = (v) => {
    const r = Number(v)
    return Number.isFinite(r) && r > 0 ? r.toFixed(1) : '—'
  }
  const formatCount = (v) => {
    const n = Number(v)
    return Number.isFinite(n) ? String(n) : '—'
  }

  if ($intelligenceSourceFps) $intelligenceSourceFps.textContent = formatRate(vision?.sourceFps)
  if ($intelligenceSampledFps) $intelligenceSampledFps.textContent = formatRate(vision?.sampledFps)
  if ($intelligenceVisionQueue) $intelligenceVisionQueue.textContent = formatCount(vision?.queueDepth)
  if ($intelligenceVisionDropped) $intelligenceVisionDropped.textContent = formatCount(vision?.queueDropped)
  if ($intelligenceLatency) {
    const usec = Number(vision?.lastLatencyUsec)
    $intelligenceLatency.textContent = Number.isFinite(usec) && usec > 0
      ? `${(usec / 1000).toFixed(1)} ms`
      : '—'
  }
  if ($intelligenceArtifacts) {
    const snaps = formatCount(vision?.snapshots)
    const clips = formatCount(vision?.clips)
    $intelligenceArtifacts.textContent = `${snaps} / ${clips}`
  }
}

// ---------------------------------------------------------------------------
// Rail toggle (persistent + keyboard)
// ---------------------------------------------------------------------------

function applyRailPref () {
  // The inline boot script already set the initial value on <html> before
  // first paint. Make sure it's normalised here in case the script failed.
  if ($root.dataset.rail !== 'open' && $root.dataset.rail !== 'closed') {
    $root.dataset.rail = 'open'
  }
}

function toggleRail () {
  const next = $root.dataset.rail === 'closed' ? 'open' : 'closed'
  $root.dataset.rail = next
  try { sessionStorage.setItem(RAIL_PREF_KEY, next) } catch (_) {}
}

$railToggle.addEventListener('click', toggleRail)

// ---------------------------------------------------------------------------
// Keyboard shortcuts
// ---------------------------------------------------------------------------

document.addEventListener('keydown', (e) => {
  const tag = e.target?.tagName
  if (tag === 'INPUT' || tag === 'TEXTAREA') return
  const k = e.key.toLowerCase()

  if (k === 'e') { toggleRail(); e.preventDefault(); return }
  if (k === 'escape') {
    if (document.fullscreenElement) {
      document.exitFullscreen().catch(() => {})
    } else if ($root.dataset.rail === 'open') {
      toggleRail()
    }
    return
  }

  // Call-only shortcuts
  if (!calls?.remotePeerId) return
  if (k === 'm') { toggleMute(); e.preventDefault() }
  else if (k === 'v') { toggleCamera(); e.preventDefault() }
  else if (k === 's') { takeSnapshot(); e.preventDefault() }
  else if (k === 'f') { toggleFullscreen(); e.preventDefault() }
})

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

function toggleMute () {
  // Toggles browser speaker output (the remote <video>'s audio). On a
  // single-machine demo we keep it muted by default to avoid the
  // speaker→FaceTime-mic feedback loop. The outgoing-mic mute (relevant
  // only when broadcasting from this tab) is handled by WebRTC's
  // built-in echoCancellation constraint set on getUserMedia.
  callState.speakerMuted = !callState.speakerMuted
  syncMuteButtons()
}

function toggleCamera () {
  callState.videoMuted = !callState.videoMuted
  calls?.muteVideo(callState.videoMuted)
  syncMuteButtons()
}

function toggleFullscreen () {
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {})
  else $stage.requestFullscreen().catch(() => {})
}

function takeSnapshot () {
  if (!$remoteVideo?.videoWidth) return
  const c = document.createElement('canvas')
  c.width = $remoteVideo.videoWidth
  c.height = $remoteVideo.videoHeight
  c.getContext('2d').drawImage($remoteVideo, 0, 0)
  c.toBlob((blob) => {
    if (!blob) return
    const url = URL.createObjectURL(blob)
    const a = document.createElement('a')
    a.href = url
    a.download = `icey-${new Date().toISOString().replace(/[:.]/g, '-')}.png`
    document.body.appendChild(a)
    a.click()
    a.remove()
    setTimeout(() => URL.revokeObjectURL(url), 1000)
  }, 'image/png')
  flashStage()
}

function flashStage () {
  if (!$snapshotFlash) return
  $snapshotFlash.classList.remove('is-flashing')
  // Force a reflow so re-adding the class triggers the animation again.
  void $snapshotFlash.offsetWidth
  $snapshotFlash.classList.add('is-flashing')
}

$btnMute.addEventListener('click', toggleMute)
$btnCamera.addEventListener('click', toggleCamera)
$btnSnapshot.addEventListener('click', takeSnapshot)
$btnFullscreen.addEventListener('click', toggleFullscreen)
$btnHangup.addEventListener('click', () => calls?.hangup())

// ---------------------------------------------------------------------------
// Idle / cursor auto-hide
// ---------------------------------------------------------------------------

const idleState = { timer: 0, idleAfterMs: 2400 }

function bumpIdle () {
  if ($stage.dataset.idle === 'true') $stage.dataset.idle = 'false'
  clearTimeout(idleState.timer)
  // Only go idle while a call is actually active. The empty state already
  // shows minimal chrome, no point fading it.
  if ($stage.dataset.empty === 'true') return
  idleState.timer = setTimeout(() => {
    $stage.dataset.idle = 'true'
  }, idleState.idleAfterMs)
}

;['mousemove', 'mousedown', 'keydown', 'touchstart', 'wheel'].forEach((evt) => {
  window.addEventListener(evt, bumpIdle, { passive: true })
})

// Re-evaluate idle when the stage transitions in or out of empty (call
// started or ended) so the cursor and chrome behave correctly across calls.
const stageObserver = new MutationObserver(() => bumpIdle())
stageObserver.observe($stage, { attributes: true, attributeFilter: ['data-empty'] })

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

function clamp (v, min, max) {
  if (!Number.isFinite(v)) return min
  return v < min ? min : (v > max ? max : v)
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

applyRailPref()
syncMuteButtons()
setStatus('connecting')
bumpIdle()

connect().catch((err) => {
  console.error('Failed to initialize icey UI:', err)
  if ($connInfo) $connInfo.textContent = 'init failed'
  setStatus('offline')
})
