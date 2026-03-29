import { SympleClient, Symple } from 'symple-client'
import { CallManager } from 'symple-player'

// ---------------------------------------------------------------------------
// DOM
// ---------------------------------------------------------------------------

const $status = document.getElementById('status')
const $peerList = document.getElementById('peer-list')
const $remoteVideo = document.getElementById('remote-video')
const $localVideo = document.getElementById('local-video')
const $overlay = document.getElementById('player-overlay')
const $controls = document.getElementById('controls')
const $connInfo = document.getElementById('connection-info')
const $statCodec = document.getElementById('stat-codec')
const $statBitrate = document.getElementById('stat-bitrate')
const $eventList = document.getElementById('event-list')
const $eventStatus = document.getElementById('event-status')

const $btnMute = document.getElementById('btn-mute')
const $btnCamera = document.getElementById('btn-camera')
const $btnFullscreen = document.getElementById('btn-fullscreen')
const $btnHangup = document.getElementById('btn-hangup')

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

let client = null
let calls = null
let audioMuted = false
let videoMuted = false
let statsInterval = null
const maxIntelligenceEvents = 12

window.__mediaServerState = {
  get client () {
    return client
  },
  get calls () {
    return calls
  },
  runtimeConfig: null
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
  if (!response.ok) {
    throw new Error(`Failed to load runtime config: ${response.status}`)
  }

  const config = await response.json()
  if (!config || config.status !== 'ok') {
    throw new Error('Invalid runtime config payload')
  }

  window.__mediaServerState.runtimeConfig = config
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
    servers.push({
      urls: `turn:${host}:${port}?transport=udp`,
      username,
      credential
    })
    servers.push({
      urls: `turn:${host}:${port}?transport=tcp`,
      username,
      credential
    })
  }

  const stunUrls = Array.isArray(config?.stun?.urls) ? config.stun.urls : []
  for (const url of stunUrls) {
    servers.push({ urls: url })
  }

  return servers
}

function formatConnectionInfo (url, user, runtimeConfig) {
  const parts = [url, user]
  if (runtimeConfig?.service) {
    parts.push(runtimeConfig.service)
  }
  if (runtimeConfig?.mode) {
    parts.push(runtimeConfig.mode)
  }
  if (runtimeConfig?.version) {
    parts.push(`v${runtimeConfig.version}`)
  }
  return parts.join('  |  ')
}

async function connect () {
  const url = getWsUrl()
  $connInfo.textContent = `Connecting to ${url}...`
  const runtimeConfig = await fetchRuntimeConfig()
  const productName = runtimeConfig?.product || 'icey'
  document.title = runtimeConfig?.mode
    ? `${productName} | ${runtimeConfig.mode}`
    : productName

  const user = 'viewer-' + Math.random().toString(36).slice(2, 6)

  client = new SympleClient({
    url,
    token: '',
    peer: {
      user,
      name: 'Browser Viewer',
      type: 'viewer'
    }
  })

  client.on('connect', () => {
    setOnline(true)
    $connInfo.textContent = formatConnectionInfo(url, user, runtimeConfig)
    updatePeerList()
  })

  client.on('presence', (p) => {
    updatePeerList()
  })

  client.on('addPeer', () => {
    updatePeerList()
  })

  client.on('removePeer', () => {
    updatePeerList()
  })

  client.on('disconnect', () => {
    setOnline(false)
    $connInfo.textContent = 'Disconnected'
    $peerList.innerHTML = ''
    resetIntelligenceFeed('Disconnected')
  })

  // Set up call manager
  calls = new CallManager(client, $remoteVideo, {
    rtcConfig: {
      iceServers: buildIceServers(runtimeConfig)
    },
    mediaConstraints: {
      audio: true,
      video: true
    },
    localMedia: false
  })

  calls.on('incoming', (peerId, msg) => {
    // Auto-accept incoming calls from the server peer.
    console.log('Incoming call from', peerId)
    calls.accept()
  })

  calls.on('active', (peerId) => {
    console.log('Call active with', peerId)
    showControls(true)
    $overlay.classList.add('hidden')
    resetIntelligenceFeed('Listening for live events')
    startStats()
  })

  calls.on('localstream', (stream) => {
    $localVideo.srcObject = stream
  })

  calls.on('ended', (peerId, reason) => {
    console.log('Call ended:', reason)
    showControls(false)
    $overlay.classList.remove('hidden')
    $remoteVideo.srcObject = null
    $localVideo.srcObject = null
    resetIntelligenceFeed('Waiting for stream')
    stopStats()
    updatePeerList()
  })

  calls.on('error', (err) => {
    console.error('Call error:', err)
  })

  client.on('error', (err) => {
    console.error('Client error:', err)
  })

  client.on('message', (message) => {
    handleIntelligenceMessage(message)
  })

  client.connect()
}

// ---------------------------------------------------------------------------
// Peer list
// ---------------------------------------------------------------------------

function updatePeerList () {
  if (!client || !client.roster) return

  $peerList.innerHTML = ''
  const peers = client.roster.data

  for (const peer of peers) {
    // Skip self
    if (peer.id === client.peer?.id) continue

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

    const type = document.createElement('span')
    type.className = 'peer-type'
    type.textContent = peer.type || ''

    nameEl.appendChild(name)
    if (peer.type) {
      nameEl.appendChild(document.createElement('br'))
      nameEl.appendChild(type)
    }

    info.appendChild(dot)
    info.appendChild(nameEl)

    const btn = document.createElement('button')
    const address = Symple.buildAddress(peer)
    const inCall = calls && calls.remotePeerId === address

    if (inCall) {
      btn.textContent = 'Hangup'
      btn.className = 'hangup'
      btn.onclick = () => calls.hangup()
      li.appendChild(info)
      li.appendChild(btn)
      $peerList.appendChild(li)
      continue
    }

    const actions = getPeerActions(peer)
    const actionBar = document.createElement('div')
    actionBar.className = 'peer-actions'
    for (const action of actions) {
      const actionBtn = document.createElement('button')
      actionBtn.textContent = action.label
      actionBtn.className = action.className || ''
      actionBtn.onclick = () => {
        console.log(`${action.label} ${address}`)
        calls.call(address, action.options)
      }
      actionBar.appendChild(actionBtn)
    }

    li.appendChild(info)
    li.appendChild(actionBar)
    $peerList.appendChild(li)
  }

  if (peers.length === 0 || (peers.length === 1 && peers[0].id === client.peer?.id)) {
    const li = document.createElement('li')
    li.className = 'peer-item'
    li.innerHTML = '<span class="peer-type">No peers online</span>'
    $peerList.appendChild(li)
  }
}

function getPeerActions (peer) {
  const capabilities = Array.isArray(peer.capabilities) ? peer.capabilities : []
  const mode = typeof peer.mode === 'string' ? peer.mode : ''
  const publishConstraints = mode === 'record'
    ? { audio: false, video: true }
    : { audio: true, video: true }
  const watchConstraints = { audio: true, video: true }

  if (capabilities.includes('publish') && capabilities.includes('view')) {
    return [
      {
        label: 'Broadcast',
        options: {
          localMedia: true,
          receiveMedia: false,
          mediaConstraints: publishConstraints
        }
      },
      {
        label: 'Watch',
        options: {
          localMedia: false,
          mediaConstraints: watchConstraints
        }
      }
    ]
  }

  if (capabilities.includes('publish')) {
    return [{
      label: 'Broadcast',
      options: {
        localMedia: true,
        receiveMedia: false,
        mediaConstraints: publishConstraints
      }
    }]
  }

  if (capabilities.includes('view')) {
    return [{
      label: 'Watch',
      options: {
        localMedia: false,
        mediaConstraints: watchConstraints
      }
    }]
  }

  return [{
      label: 'Call',
      options: {
        localMedia: true,
        receiveMedia: true,
        mediaConstraints: watchConstraints
      }
    }]
}

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

function showControls (visible) {
  $controls.classList.toggle('hidden', !visible)
}

function resetIntelligenceFeed (statusText) {
  if ($eventStatus) {
    $eventStatus.textContent = statusText
  }

  if ($eventList) {
    $eventList.innerHTML = '<li class="event-empty">No events yet</li>'
  }
}

function handleIntelligenceMessage (message) {
  const subtype = message?.subtype
  if (subtype !== 'icey:vision' && subtype !== 'icey:speech') {
    return
  }

  const kind = subtype === 'icey:vision' ? 'vision' : 'speech'
  renderIntelligenceEvent(kind, message?.data || {})
}

function renderIntelligenceEvent (kind, event) {
  if (!$eventList) return

  const empty = $eventList.querySelector('.event-empty')
  if (empty) {
    empty.remove()
  }

  if ($eventStatus) {
    $eventStatus.textContent = kind === 'vision'
      ? 'Vision events live'
      : 'Speech events live'
  }

  const item = document.createElement('li')
  item.className = `event-item ${kind}`

  const meta = document.createElement('div')
  meta.className = 'event-meta'

  const label = document.createElement('span')
  label.className = 'event-label'
  label.textContent = kind === 'vision' ? 'Vision' : 'Speech'

  const time = document.createElement('span')
  time.textContent = formatEventTime(event)

  meta.appendChild(label)
  meta.appendChild(time)

  const summary = document.createElement('div')
  summary.className = 'event-summary'
  summary.textContent = describeIntelligenceEvent(kind, event)

  item.appendChild(meta)
  item.appendChild(summary)
  $eventList.prepend(item)

  while ($eventList.children.length > maxIntelligenceEvents) {
    $eventList.removeChild($eventList.lastElementChild)
  }
}

function formatEventTime (event) {
  const usec = Number(event?.time || event?.audio?.time || event?.frame?.time || 0)
  if (!Number.isFinite(usec) || usec <= 0) {
    return 'live'
  }

  return `${(usec / 1000000).toFixed(2)}s`
}

function describeIntelligenceEvent (kind, event) {
  if (kind === 'vision') {
    const detection = Array.isArray(event?.detections) ? event.detections[0] : null
    const label = detection?.label || event?.detector || 'detection'
    const score = Number(detection?.confidence ?? event?.data?.score ?? 0)
    return `${label} ${(score * 100).toFixed(1)}% confidence`
  }

  const level = Number(event?.level ?? 0)
  const state = typeof event?.type === 'string' ? event.type.replace('speech:', '') : 'event'
  return `${state} at ${(level * 100).toFixed(1)}% level`
}

$btnMute.addEventListener('click', () => {
  audioMuted = !audioMuted
  calls?.muteAudio(audioMuted)
  $btnMute.textContent = audioMuted ? '🔊' : '🔇'
})

$btnCamera.addEventListener('click', () => {
  videoMuted = !videoMuted
  calls?.muteVideo(videoMuted)
  $btnCamera.style.opacity = videoMuted ? '0.5' : '1'
})

$btnFullscreen.addEventListener('click', () => {
  const container = document.getElementById('player-container')
  if (document.fullscreenElement) {
    document.exitFullscreen()
  } else {
    container.requestFullscreen()
  }
})

$btnHangup.addEventListener('click', () => {
  calls?.hangup()
})

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

function startStats () {
  stopStats()
  statsInterval = setInterval(async () => {
    if (!calls?.player?.pc) return

    try {
      const stats = await calls.player.pc.getStats()
      for (const report of stats.values()) {
        if (report.type === 'inbound-rtp' && report.kind === 'video') {
          const codec = report.decoderImplementation || ''
          const width = report.frameWidth
          const height = report.frameHeight
          $statCodec.textContent = codec && width ? `${codec} ${width}x${height}` : ''
        }
        if (report.type === 'candidate-pair' && report.state === 'succeeded') {
          const bitrate = report.availableIncomingBitrate
          if (bitrate) {
            $statBitrate.textContent = `${(bitrate / 1e6).toFixed(1)} Mbps`
          }
        }
      }
    } catch (_) { /* stats may fail during teardown */ }
  }, 2000)
}

function stopStats () {
  if (statsInterval) {
    clearInterval(statsInterval)
    statsInterval = null
  }
  $statCodec.textContent = ''
  $statBitrate.textContent = ''
}

// ---------------------------------------------------------------------------
// UI helpers
// ---------------------------------------------------------------------------

function setOnline (online) {
  $status.textContent = online ? 'Online' : 'Offline'
  $status.className = 'status ' + (online ? 'online' : 'offline')
}

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------

connect().catch((err) => {
  console.error('Failed to initialize icey UI:', err)
  $connInfo.textContent = 'Initialization failed'
})
