// State
let ws = null;
let wsReconnectTimer = null;
let currentWizardStep = 1;
let topology = null;
let selectedReader = null;
let config = {};
let gpioConfig = [];  // GPIO configuration array
let mqttTriggers = [];  // MQTT trigger configuration array
let isApMode = false;

// Initialize
document.addEventListener('DOMContentLoaded', () => {
    checkMode();
    initTabs();
    initToggles();
    initLoginForm();
});

// Check device mode (AP mode vs normal)
async function checkMode() {
    try {
        const response = await fetch('/api/mode');
        const data = await response.json();

        isApMode = data.apMode;

        if (isApMode) {
            // In AP mode - show simple WiFi setup
            showApSetup();
            initApSetupForm();
        } else {
            // Normal mode - check authentication
            checkAuth();
        }
    } catch (e) {
        console.error('Mode check failed:', e);
        // Fallback to normal auth check
        checkAuth();
    }
}

// Check authentication status
async function checkAuth() {
    try {
        const response = await fetch('/api/auth/status');
        const data = await response.json();

        if (data.authenticated) {
            showApp();
            loadVersion();
            connectWebSocket();
            loadConfig();
        } else {
            showLogin();
        }
    } catch (e) {
        console.error('Auth check failed:', e);
        showLogin();
    }
}

// Show AP setup screen
function showApSetup() {
    const apScreen = document.getElementById('ap-setup-screen');
    const loginScreen = document.getElementById('login-screen');
    const app = document.getElementById('app');

    apScreen.classList.remove('hidden');
    apScreen.style.display = 'flex';
    loginScreen.classList.add('hidden');
    loginScreen.style.display = 'none';
    app.classList.remove('active');
}

// Show login screen
function showLogin() {
    const apScreen = document.getElementById('ap-setup-screen');
    const loginScreen = document.getElementById('login-screen');
    const app = document.getElementById('app');

    apScreen.classList.add('hidden');
    apScreen.style.display = 'none';
    loginScreen.classList.remove('hidden');
    loginScreen.style.display = 'flex';
    app.classList.remove('active');

    // Focus the username field for better UX
    setTimeout(() => {
        const usernameInput = document.getElementById('login-username');
        if (usernameInput) usernameInput.focus();
    }, 100);
}

// Show main app
function showApp() {
    const apScreen = document.getElementById('ap-setup-screen');
    const loginScreen = document.getElementById('login-screen');
    const app = document.getElementById('app');

    apScreen.classList.add('hidden');
    apScreen.style.display = 'none';
    loginScreen.classList.add('hidden');
    loginScreen.style.display = 'none';
    app.classList.add('active');
}

// Initialize AP setup form
function initApSetupForm() {
    const form = document.getElementById('ap-setup-form');
    if (!form) return;

    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const ssid = document.getElementById('ap-wifi-ssid').value.trim();
        const password = document.getElementById('ap-wifi-password').value;
        const errorDiv = document.getElementById('ap-setup-error');
        const successDiv = document.getElementById('ap-setup-success');
        const submitBtn = form.querySelector('button[type="submit"]');

        errorDiv.classList.add('hidden');
        successDiv.classList.add('hidden');

        if (!ssid) {
            errorDiv.textContent = 'Please enter your WiFi network name';
            errorDiv.classList.remove('hidden');
            return;
        }

        submitBtn.disabled = true;
        submitBtn.innerHTML = '<span class="spinner"></span> Testing connection...';

        try {
            // Start the WiFi test (non-blocking)
            await fetch('/api/wifi/test', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ ssid, password })
            });

            // Poll for connection status
            let testResult = null;
            for (let i = 0; i < 35; i++) {  // Poll for up to 17.5 seconds
                await new Promise(resolve => setTimeout(resolve, 500));

                const statusResponse = await fetch('/api/wifi/status');
                const status = await statusResponse.json();

                if (status.status === 'success') {
                    testResult = status;
                    break;
                } else if (status.status === 'failed') {
                    testResult = status;
                    break;
                }
                // Still connecting, continue polling
            }

            if (!testResult) {
                testResult = { success: false, message: 'Connection timed out' };
            }

            if (!testResult.success) {
                // Connection failed - show error and let user retry
                errorDiv.textContent = testResult.message || 'Could not connect to WiFi. Please check your credentials.';
                errorDiv.classList.remove('hidden');
                submitBtn.disabled = false;
                submitBtn.innerHTML = 'Connect';
                return;
            }

            // Test successful - save and reboot
            submitBtn.innerHTML = '<span class="spinner"></span> Saving...';
            successDiv.textContent = 'Connection successful! IP: ' + (testResult.ip || 'assigned');
            successDiv.classList.remove('hidden');

            // Short delay to show success, then save and reboot
            await new Promise(resolve => setTimeout(resolve, 1000));

            submitBtn.innerHTML = '<span class="spinner"></span> Rebooting...';
            successDiv.textContent = 'Saving configuration and rebooting...';

            // Send the setup request - device will reboot
            fetch('/api/wifi/setup', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ ssid, password })
            }).catch(() => {
                // Expected - device reboots and connection drops
            });

            // Show final message with clickable links and reload button
            setTimeout(() => {
                const ip = testResult.ip || '';
                successDiv.style.flexDirection = 'column';
                successDiv.style.alignItems = 'flex-start';
                successDiv.innerHTML = `
                    <div>Device is rebooting.</div>
                    <div style="margin-top:8px">Connect to your WiFi and open:</div>
                    <a href="http://doorbell.local" style="color:var(--accent-blue)">doorbell.local</a>
                    ${ip ? `<a href="http://${ip}" style="color:var(--accent-blue)">${ip}</a>` : ''}
                    <button class="btn btn-primary" onclick="location.reload()" style="margin-top:12px">Reload</button>
                `;
                submitBtn.style.display = 'none';
            }, 2000);

        } catch (e) {
            errorDiv.textContent = 'Error: ' + e.message;
            errorDiv.classList.remove('hidden');
            submitBtn.disabled = false;
            submitBtn.innerHTML = 'Connect';
        }
    });
}

// Initialize login form
function initLoginForm() {
    const form = document.getElementById('login-form');
    if (!form) {
        console.error('Login form not found');
        return;
    }

    form.addEventListener('submit', async (e) => {
        e.preventDefault();
        const username = document.getElementById('login-username').value;
        const password = document.getElementById('login-password').value;
        const errorDiv = document.getElementById('login-error');

        console.log('Attempting login with username:', username);

        try {
            const response = await fetch('/api/auth/login', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ username, password })
            });

            console.log('Login response status:', response.status);
            const data = await response.json();
            console.log('Login response data:', data);

            if (data.success) {
                console.log('Login successful, showing app');
                if (errorDiv) errorDiv.classList.add('hidden');
                showApp();
                loadVersion();
                connectWebSocket();
                loadConfig();
            } else {
                console.log('Login failed:', data.message);
                if (errorDiv) {
                    errorDiv.textContent = data.message || 'Invalid credentials';
                    errorDiv.classList.remove('hidden');
                }
            }
        } catch (e) {
            console.error('Login error:', e);
            if (errorDiv) {
                errorDiv.textContent = 'Connection failed: ' + e.message;
                errorDiv.classList.remove('hidden');
            }
        }
    });
}

// Logout
async function logout() {
    try {
        await fetch('/api/auth/logout', { method: 'POST' });
    } catch (e) {}
    if (ws) ws.close();
    showLogin();
}

// Tab navigation
function initTabs() {
    document.querySelectorAll('.nav-tab').forEach(tab => {
        tab.addEventListener('click', () => {
            const tabName = tab.dataset.tab;

            // Update tab buttons
            document.querySelectorAll('.nav-tab').forEach(t => t.classList.remove('active'));
            tab.classList.add('active');

            // Update panels
            document.querySelectorAll('.tab-panel').forEach(p => p.classList.remove('active'));
            document.getElementById('tab-' + tabName).classList.add('active');

            // Load topology when entering setup tab
            if (tabName === 'setup' && currentWizardStep >= 3) {
                loadTopology();
            }
        });
    });
}

// Toggle switches
function initToggles() {
    const mqttEnabled = document.getElementById('mqtt-enabled');
    const mqttAuth = document.getElementById('mqtt-auth');

    if (mqttEnabled) {
        mqttEnabled.addEventListener('change', function() {
            const settings = document.getElementById('mqtt-settings');
            const triggersCard = document.getElementById('mqtt-triggers-card');
            if (settings) settings.classList.toggle('hidden', !this.checked);
            if (triggersCard) triggersCard.classList.toggle('hidden', !this.checked);
        });
    }

    if (mqttAuth) {
        mqttAuth.addEventListener('change', function() {
            const fields = document.getElementById('mqtt-auth-fields');
            if (fields) fields.classList.toggle('hidden', !this.checked);
        });
    }
}

// WebSocket connection
function connectWebSocket() {
    const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
    ws = new WebSocket(`${protocol}//${location.host}/ws`);

    ws.onopen = () => {
        const dot = document.getElementById('ws-status-dot');
        const text = document.getElementById('ws-status-text');
        if (dot) dot.classList.remove('disconnected');
        if (text) text.textContent = 'Connected';
        if (wsReconnectTimer) {
            clearTimeout(wsReconnectTimer);
            wsReconnectTimer = null;
        }
    };

    ws.onclose = () => {
        const dot = document.getElementById('ws-status-dot');
        const text = document.getElementById('ws-status-text');
        if (dot) dot.classList.add('disconnected');
        if (text) text.textContent = 'Disconnected';
        wsReconnectTimer = setTimeout(connectWebSocket, 3000);
    };

    ws.onerror = () => ws.close();

    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            handleMessage(data);
        } catch (e) {
            // Ignore parse errors during initial load
        }
    };
}

// Handle WebSocket messages
function handleMessage(data) {
    if (data.type === 'status') {
        updateStatus(data);
    } else if (data.type === 'doorbell') {
        handleDoorbellEvent(data);
    }
}

// Update status display
function updateStatus(data) {
    // Check if app is visible first
    const app = document.getElementById('app');
    if (!app || !app.classList.contains('active')) return;

    // Doorbell
    const doorbellEl = document.getElementById('status-doorbell');
    const ringAlert = document.getElementById('ring-alert');
    if (!doorbellEl || !ringAlert) return;

    if (data.doorbell?.active) {
        doorbellEl.textContent = 'Ringing';
        doorbellEl.className = 'value ringing';
        ringAlert.classList.add('active');
    } else {
        doorbellEl.textContent = 'Idle';
        doorbellEl.className = 'value';
        ringAlert.classList.remove('active');
    }

    // Network
    const networkEl = document.getElementById('status-network');
    if (networkEl && data.network?.connected) {
        networkEl.textContent = data.network.type === 'ethernet' ? 'Ethernet' : 'WiFi';
        networkEl.className = 'value connected';
        // Hide WiFi settings for Ethernet devices
        const wifiCard = document.getElementById('wifi-card');
        if (wifiCard && data.network.type === 'ethernet') {
            wifiCard.classList.add('hidden');
        }
    } else if (networkEl) {
        networkEl.textContent = 'Disconnected';
        networkEl.className = 'value disconnected';
    }

    // UniFi
    const unifiEl = document.getElementById('status-unifi');
    if (unifiEl) {
        if (data.unifi?.wsConnected) {
            unifiEl.textContent = 'Connected';
            unifiEl.className = 'value connected';
        } else if (data.unifi?.loggedIn) {
            unifiEl.textContent = 'Logged In';
            unifiEl.className = 'value';
        } else if (data.unifi?.error) {
            unifiEl.textContent = data.unifi.error;
            unifiEl.className = 'value disconnected';
        } else if (data.unifi?.configured) {
            unifiEl.textContent = 'Logging In...';
            unifiEl.className = 'value';
        } else {
            unifiEl.textContent = '--';
            unifiEl.className = 'value';
        }
    }

    // MQTT
    const mqttEl = document.getElementById('status-mqtt');
    if (mqttEl) {
        if (data.mqtt?.connected) {
            mqttEl.textContent = 'Connected';
            mqttEl.className = 'value connected';
        } else {
            mqttEl.textContent = 'Disabled';
            mqttEl.className = 'value';
        }
    }

    // Heap
    const heapEl = document.getElementById('status-heap');
    if (heapEl && data.system?.heap) {
        const heapKB = Math.round(data.system.heap / 1024);
        heapEl.textContent = `${heapKB} KB`;
    }

    // Uptime
    const uptimeEl = document.getElementById('status-uptime');
    if (uptimeEl && data.system?.uptime !== undefined) {
        const hours = Math.floor(data.system.uptime / 3600);
        const minutes = Math.floor((data.system.uptime % 3600) / 60);
        uptimeEl.textContent = `${hours}h ${minutes}m`;
    }
}

// Handle doorbell events
function handleDoorbellEvent(data) {
    // Check if app is visible first
    const app = document.getElementById('app');
    if (!app || !app.classList.contains('active')) return;

    const ringAlert = document.getElementById('ring-alert');
    const doorbellEl = document.getElementById('status-doorbell');
    if (!ringAlert || !doorbellEl) return;

    if (data.event === 'ring') {
        doorbellEl.textContent = 'Ringing';
        doorbellEl.className = 'value ringing';
        ringAlert.classList.add('active');
    } else if (data.event === 'idle') {
        doorbellEl.textContent = 'Idle';
        doorbellEl.className = 'value';
        ringAlert.classList.remove('active');
    }
}

// Load firmware version
async function loadVersion() {
    try {
        const response = await fetch('/api/version');
        const data = await response.json();
        document.getElementById('firmware-version').textContent = data.version || '--';
    } catch (e) {}
}

// Load configuration
async function loadConfig() {
    try {
        const response = await fetch('/api/config');
        config = await response.json();
        populateForm(config);
        populateWizardFromConfig(config);
    } catch (e) {
        console.error('Failed to load config:', e);
    }

    // Also load certificate
    try {
        const response = await fetch('/api/cert');
        const data = await response.json();
        if (data.certificate) {
            document.getElementById('certificate').value = data.certificate;
        }
    } catch (e) {}
}

// Populate settings form
function populateForm(config) {
    // WiFi
    if (config.network) {
        const wifiSsid = document.getElementById('wifi-ssid');
        if (wifiSsid) wifiSsid.value = config.network.wifiSsid || '';
    }

    // MQTT
    if (config.mqtt) {
        const mqttEnabled = document.getElementById('mqtt-enabled');
        const mqttSettings = document.getElementById('mqtt-settings');
        const mqttServer = document.getElementById('mqtt-server');
        const mqttPort = document.getElementById('mqtt-port');
        const mqttTopic = document.getElementById('mqtt-topic');
        const mqttAuth = document.getElementById('mqtt-auth');
        const mqttAuthFields = document.getElementById('mqtt-auth-fields');
        const mqttUsername = document.getElementById('mqtt-username');

        if (mqttEnabled) mqttEnabled.checked = config.mqtt.enabled || false;
        if (mqttSettings) mqttSettings.classList.toggle('hidden', !config.mqtt.enabled);
        if (mqttServer) mqttServer.value = config.mqtt.server || '';
        if (mqttPort) mqttPort.value = config.mqtt.port || 1883;
        if (mqttTopic) mqttTopic.value = config.mqtt.topic || '';
        if (mqttAuth) mqttAuth.checked = config.mqtt.authEnabled || false;
        if (mqttAuthFields) mqttAuthFields.classList.toggle('hidden', !config.mqtt.authEnabled);
        if (mqttUsername) mqttUsername.value = config.mqtt.username || '';
    }

    // Web UI
    if (config.web) {
        const webUsername = document.getElementById('web-username');
        if (webUsername) webUsername.value = config.web.username || '';
    }

    // GPIO
    if (config.gpios) {
        gpioConfig = config.gpios;
        populateGpioList();
    }

    // MQTT Triggers
    if (config.mqttTriggers) {
        mqttTriggers = config.mqttTriggers;
        populateMqttTriggersList();
    }

    // Show/hide MQTT triggers card based on MQTT enabled
    const mqttTriggersCard = document.getElementById('mqtt-triggers-card');
    if (mqttTriggersCard) {
        mqttTriggersCard.classList.toggle('hidden', !config.mqtt?.enabled);
    }
}

// Populate wizard from existing config
function populateWizardFromConfig(config) {
    if (config.unifi) {
        const unifiHost = document.getElementById('unifi-host');
        const unifiUsername = document.getElementById('unifi-username');
        const unifiPassword = document.getElementById('unifi-password');
        const certHostHint = document.getElementById('cert-host-hint');

        if (unifiHost) unifiHost.value = config.unifi.host || '';
        if (unifiUsername) unifiUsername.value = config.unifi.username || '';
        if (certHostHint && config.unifi.host) {
            certHostHint.textContent = config.unifi.host;
        }

        // If password is set (shown as masked value), update placeholder
        if (unifiPassword && config.unifi.password && config.unifi.password.includes('*')) {
            unifiPassword.placeholder = 'Password is set (leave blank to keep)';
        }
    }

    if (config.doorbell) {
        const doorbellName = document.getElementById('doorbell-name');
        const doorName = document.getElementById('door-name');

        if (doorbellName) doorbellName.value = config.doorbell.deviceName || '';
        if (doorName) doorName.value = config.doorbell.doorName || '';
        selectedReader = config.doorbell.deviceId || null;
    }
}

// =============================================================================
// Wizard Navigation
// =============================================================================

function goToWizardStep(step) {
    // Update step indicators
    document.querySelectorAll('.wizard-step').forEach(el => {
        const stepNum = parseInt(el.dataset.step);
        el.classList.remove('active', 'complete');
        if (stepNum < step) el.classList.add('complete');
        if (stepNum === step) el.classList.add('active');
    });

    // Show/hide step content
    for (let i = 1; i <= 4; i++) {
        document.getElementById('wizard-step-' + i).classList.toggle('hidden', i !== step);
    }

    currentWizardStep = step;

    // Load data for specific steps
    if (step === 3) {
        loadTopology();
    }

    if (step === 4) {
        updateSetupSummary();
    }
}

async function wizardNext(currentStep) {
    // Validate current step
    if (currentStep === 1) {
        const host = document.getElementById('unifi-host').value.trim();
        const username = document.getElementById('unifi-username').value.trim();
        const password = document.getElementById('unifi-password').value;

        // Check if password is already set (placeholder indicates this)
        const passwordInput = document.getElementById('unifi-password');
        const hasExistingPassword = passwordInput && passwordInput.placeholder.includes('is set');

        if (!host || !username || (!password && !hasExistingPassword)) {
            alert('Please fill in all UniFi Access fields');
            return;
        }

        // Save config to device
        await saveWizardStep1();

        // Update cert help text
        document.getElementById('cert-host-hint').textContent = host;
    }

    if (currentStep === 2) {
        const cert = document.getElementById('certificate').value.trim();
        if (cert && cert.includes('BEGIN CERTIFICATE')) {
            await saveCertificate(cert);
        }
    }

    if (currentStep === 3) {
        if (!selectedReader) {
            alert('Please select a doorbell device');
            return;
        }
    }

    goToWizardStep(currentStep + 1);
}

function wizardBack(currentStep) {
    goToWizardStep(currentStep - 1);
}

async function saveWizardStep1() {
    const password = document.getElementById('unifi-password').value;

    const configUpdate = {
        unifi: {
            host: document.getElementById('unifi-host').value.trim(),
            username: document.getElementById('unifi-username').value.trim()
        }
    };

    // Only include password if user entered a new one
    if (password) {
        configUpdate.unifi.password = password;
    }

    try {
        await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(configUpdate)
        });
    } catch (e) {
        console.error('Failed to save config:', e);
    }
}

// =============================================================================
// Certificate Management
// =============================================================================

async function fetchCertificate() {
    const spinner = document.getElementById('cert-spinner');
    const statusDiv = document.getElementById('cert-status');

    spinner.classList.remove('hidden');
    statusDiv.classList.add('hidden');

    try {
        const response = await fetch('/api/fetchcert', { method: 'POST' });
        const data = await response.json();

        if (data.success && data.certificate) {
            document.getElementById('certificate').value = data.certificate;
            await saveCertificate(data.certificate);
            statusDiv.textContent = 'Certificate fetched and saved successfully!';
            statusDiv.className = 'alert alert-success';
            statusDiv.classList.remove('hidden');
        } else {
            statusDiv.textContent = 'Failed: ' + (data.message || 'Unknown error');
            statusDiv.className = 'alert alert-error';
            statusDiv.classList.remove('hidden');
        }
    } catch (e) {
        statusDiv.textContent = 'Error: ' + e.message;
        statusDiv.className = 'alert alert-error';
        statusDiv.classList.remove('hidden');
    }

    spinner.classList.add('hidden');
}

async function saveCertificate(cert) {
    try {
        await fetch('/api/cert', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ certificate: cert })
        });
    } catch (e) {
        console.error('Failed to save certificate:', e);
    }
}

async function testConnection() {
    const statusDiv = document.getElementById('cert-status');
    statusDiv.textContent = 'Testing connection...';
    statusDiv.className = 'alert alert-info';
    statusDiv.classList.remove('hidden');

    try {
        const response = await fetch('/api/test', { method: 'POST' });
        const data = await response.json();

        if (data.success) {
            statusDiv.textContent = data.message;
            statusDiv.className = 'alert alert-success';
        } else {
            statusDiv.textContent = data.message;
            statusDiv.className = 'alert alert-error';
        }
    } catch (e) {
        statusDiv.textContent = 'Error: ' + e.message;
        statusDiv.className = 'alert alert-error';
    }
}

// =============================================================================
// Topology / Device Selection
// =============================================================================

async function loadTopology() {
    const readersLoading = document.getElementById('readers-loading');
    const readersList = document.getElementById('readers-list');
    const readersError = document.getElementById('readers-error');

    // Show loading
    readersLoading?.classList.remove('hidden');
    readersList?.classList.add('hidden');
    readersError?.classList.add('hidden');

    try {
        const response = await fetch('/api/topology');
        topology = await response.json();

        if (!topology.success) {
            throw new Error(topology.message || 'Failed to load devices');
        }

        // Populate readers
        if (readersList) {
            readersList.innerHTML = '';
            (topology.readers || []).forEach(device => {
                const isSelected = selectedReader === device.id;
                const div = document.createElement('div');
                div.className = 'device-item' + (isSelected ? ' selected' : '');
                div.innerHTML = `
                    <input type="radio" name="reader" value="${device.id}" ${isSelected ? 'checked' : ''}>
                    <div class="device-info">
                        <div class="name">${device.name || 'Unnamed Device'}</div>
                        <div class="meta">${device.type} - ${device.mac}</div>
                    </div>
                `;
                div.addEventListener('click', () => selectReader(device.id, div));
                readersList.appendChild(div);
            });

            readersLoading.classList.add('hidden');
            if (topology.readers?.length > 0) {
                readersList.classList.remove('hidden');
                document.getElementById('manual-reader-section')?.classList.add('hidden');
            } else {
                readersError.textContent = 'No reader devices found. You can enter a device ID/MAC manually below.';
                readersError.classList.remove('hidden');
                document.getElementById('manual-reader-section')?.classList.remove('hidden');
            }
        }

    } catch (e) {
        console.error('Failed to load topology:', e);
        if (readersError) {
            readersError.innerHTML = `
                <div style="display:flex; flex-direction:column; gap:12px;">
                    <div>Failed to load devices: ${e.message}</div>
                    <div style="display:flex; gap:12px;">
                        <button class="btn btn-primary" onclick="loadTopology()">Retry</button>
                        <span style="color:var(--text-muted)">or enter a device ID/MAC manually below</span>
                    </div>
                </div>
            `;
            readersError.classList.remove('hidden');
        }
        readersLoading?.classList.add('hidden');
        document.getElementById('manual-reader-section')?.classList.remove('hidden');
    }
}

function setManualReader() {
    const input = document.getElementById('manual-reader-id');
    const value = input?.value?.trim();
    if (!value) {
        alert('Please enter a device ID or MAC address');
        return;
    }
    selectedReader = value;
    // Update UI to show selection
    const readersError = document.getElementById('readers-error');
    if (readersError) {
        readersError.textContent = 'Manual entry: ' + value;
        readersError.className = 'device-error alert-info';
    }
    // Clear the readers list and show confirmation
    const readersList = document.getElementById('readers-list');
    if (readersList) {
        readersList.innerHTML = `<div class="device-item selected">
            <div class="device-info">
                <div class="name">Manual Entry</div>
                <div class="meta">${value}</div>
            </div>
        </div>`;
        readersList.classList.remove('hidden');
    }
}

function selectReader(deviceId, element) {
    selectedReader = deviceId;
    document.querySelectorAll('#readers-list .device-item').forEach(el => {
        el.classList.remove('selected');
        el.querySelector('input').checked = false;
    });
    element.classList.add('selected');
    element.querySelector('input').checked = true;

    // Auto-fill device name if empty
    const device = topology?.readers?.find(d => d.id === deviceId);
    if (device && !document.getElementById('doorbell-name').value) {
        document.getElementById('doorbell-name').value = device.name || '';
    }
}

// =============================================================================
// Setup Summary & Save
// =============================================================================

function updateSetupSummary() {
    const summary = document.getElementById('setup-summary');
    const host = document.getElementById('unifi-host').value;
    const readerName = topology?.readers?.find(r => r.id === selectedReader)?.name || selectedReader;

    // Get parent alert box and set flex direction
    const alertBox = summary.parentElement;
    if (alertBox) {
        alertBox.style.flexDirection = 'column';
        alertBox.style.alignItems = 'flex-start';
    }

    summary.innerHTML = `
        <div style="margin-bottom:8px"><strong>UniFi Host:</strong><br>${host}</div>
        <div><strong>Doorbell Device:</strong><br>${readerName || 'Not selected'}</div>
    `;
}

async function saveAndReboot() {
    const btn = document.querySelector('#wizard-step-4 .btn-primary');
    const statusDiv = document.getElementById('save-status');

    // Show saving state
    if (btn) {
        btn.disabled = true;
        btn.innerHTML = '<span class="spinner"></span> Saving...';
    }
    if (statusDiv) {
        statusDiv.textContent = 'Saving configuration...';
        statusDiv.className = 'alert alert-info';
        statusDiv.classList.remove('hidden');
    }

    const password = document.getElementById('unifi-password').value;

    const configUpdate = {
        unifi: {
            host: document.getElementById('unifi-host').value.trim(),
            username: document.getElementById('unifi-username').value.trim()
        },
        doorbell: {
            deviceId: selectedReader || '',
            deviceName: document.getElementById('doorbell-name').value.trim(),
            doorName: document.getElementById('door-name').value.trim()
        }
    };

    // Only include password if user entered a new one
    if (password) {
        configUpdate.unifi.password = password;
    }

    try {
        await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(configUpdate)
        });

        if (statusDiv) {
            statusDiv.textContent = 'Configuration saved! Rebooting device...';
            statusDiv.className = 'alert alert-success';
        }
        if (btn) {
            btn.innerHTML = 'Rebooting...';
        }

        await fetch('/api/control/reboot', { method: 'POST' });

        if (statusDiv) {
            statusDiv.textContent = 'Device is rebooting. Page will refresh in 10 seconds...';
        }
        setTimeout(() => location.reload(), 10000);
    } catch (e) {
        if (statusDiv) {
            statusDiv.textContent = 'Error: ' + e.message;
            statusDiv.className = 'alert alert-error';
        }
        if (btn) {
            btn.disabled = false;
            btn.innerHTML = 'Save & Reboot';
        }
    }
}

// =============================================================================
// GPIO Configuration
// =============================================================================

function populateGpioList() {
    const container = document.getElementById('gpio-list');
    if (!container) return;

    container.innerHTML = '';

    gpioConfig.forEach((gpio, index) => {
        const div = document.createElement('div');
        div.className = 'gpio-item';
        div.innerHTML = `
            <div class="gpio-header">
                <span class="gpio-title">GPIO ${gpio.pin} - ${gpio.label || 'Unnamed'}</span>
                <button class="btn btn-danger btn-sm" onclick="removeGpio(${index})">Remove</button>
            </div>
            <div class="gpio-fields">
                <div class="form-group">
                    <label>Pin Number</label>
                    <input type="number" value="${gpio.pin}" min="0" max="48"
                           onchange="updateGpio(${index}, 'pin', parseInt(this.value))">
                </div>
                <div class="form-group">
                    <label>Label</label>
                    <input type="text" value="${gpio.label || ''}"
                           onchange="updateGpio(${index}, 'label', this.value)">
                </div>
                <div class="form-group">
                    <label>Action</label>
                    <select onchange="updateGpio(${index}, 'action', this.value); updateGpioHelp(${index})">
                        <option value="ring_button" ${gpio.action === 'ring_button' ? 'selected' : ''}>Ring Button</option>
                        <option value="door_contact" ${gpio.action === 'door_contact' ? 'selected' : ''}>Door Contact</option>
                        <option value="generic" ${gpio.action === 'generic' ? 'selected' : ''}>Generic (MQTT)</option>
                    </select>
                </div>
                <div class="form-group">
                    <label>Pull Mode</label>
                    <select onchange="updateGpio(${index}, 'pullMode', this.value)">
                        <option value="up" ${gpio.pullMode === 'up' ? 'selected' : ''}>Pull-Up (Active LOW)</option>
                        <option value="down" ${gpio.pullMode === 'down' ? 'selected' : ''}>Pull-Down (Active HIGH)</option>
                    </select>
                </div>
            </div>
            <div class="gpio-help" id="gpio-help-${index}">
                ${getGpioHelpText(gpio.action, gpio.pullMode, gpio.label)}
            </div>
        `;
        container.appendChild(div);
    });

    // Add some inline styles for GPIO items
    if (!document.getElementById('gpio-styles')) {
        const style = document.createElement('style');
        style.id = 'gpio-styles';
        style.textContent = `
            .gpio-item {
                background: var(--bg-tertiary);
                border: 1px solid var(--border-color);
                border-radius: var(--radius);
                padding: 16px;
                margin-bottom: 12px;
            }
            .gpio-header {
                display: flex;
                justify-content: space-between;
                align-items: center;
                margin-bottom: 12px;
            }
            .gpio-title {
                font-weight: 600;
            }
            .gpio-fields {
                display: grid;
                grid-template-columns: repeat(4, 1fr);
                gap: 12px;
                align-items: end;
            }
            .gpio-fields .form-group {
                margin-bottom: 0;
            }
            .gpio-fields input,
            .gpio-fields select {
                height: 40px;
            }
            @media (max-width: 768px) {
                .gpio-fields {
                    grid-template-columns: repeat(2, 1fr);
                }
            }
            @media (max-width: 480px) {
                .gpio-fields {
                    grid-template-columns: 1fr;
                }
            }
            .gpio-help {
                margin-top: 12px;
                padding: 12px;
                background: rgba(47, 129, 247, 0.1);
                border-radius: var(--radius);
                font-size: 13px;
                color: var(--text-secondary);
            }
            .btn-sm {
                padding: 6px 12px;
                font-size: 12px;
            }
        `;
        document.head.appendChild(style);
    }
}

// GPIO presets for common boards
const gpioPresets = {
    'olimex-poe': {
        name: 'Olimex ESP32-POE',
        pins: [
            { pin: 34, label: 'BUT1 Button', action: 'ring_button', pullMode: 'up', note: 'Built-in button (input only)' },
            { pin: 36, label: 'GPIO36', action: 'door_contact', pullMode: 'up', note: 'Input only, 10K pullup' },
            { pin: 39, label: 'GPIO39', action: 'generic', pullMode: 'up', note: 'Input only' },
            { pin: 32, label: 'GPIO32', action: 'generic', pullMode: 'up', note: 'General purpose' },
            { pin: 33, label: 'GPIO33', action: 'generic', pullMode: 'up', note: 'General purpose' },
            { pin: 35, label: 'GPIO35', action: 'generic', pullMode: 'up', note: 'Input only' }
        ]
    },
    'esp32-s3-zero': {
        name: 'ESP32-S3-Zero',
        pins: [
            { pin: 0, label: 'BOOT Button', action: 'ring_button', pullMode: 'up', note: 'Built-in button' },
            { pin: 1, label: 'GPIO1', action: 'door_contact', pullMode: 'up', note: 'General purpose' },
            { pin: 2, label: 'GPIO2', action: 'generic', pullMode: 'up', note: 'General purpose' },
            { pin: 3, label: 'GPIO3', action: 'generic', pullMode: 'up', note: 'General purpose' },
            { pin: 4, label: 'GPIO4', action: 'generic', pullMode: 'up', note: 'General purpose' }
        ]
    }
};

// Currently selected board in the GPIO modal
let selectedGpioBoard = 'olimex-poe';

function showGpioPresetMenu() {
    // Add styles if not present
    if (!document.getElementById('gpio-preset-styles')) {
        const style = document.createElement('style');
        style.id = 'gpio-preset-styles';
        style.textContent = `
            .gpio-preset-overlay {
                position: fixed;
                top: 0;
                left: 0;
                right: 0;
                bottom: 0;
                background: rgba(0,0,0,0.6);
                z-index: 999;
            }
            .gpio-preset-menu {
                position: fixed;
                top: 50%;
                left: 50%;
                transform: translate(-50%, -50%);
                background: var(--bg-secondary);
                border: 1px solid var(--border-color);
                border-radius: var(--radius);
                box-shadow: 0 8px 32px rgba(0,0,0,0.4);
                z-index: 1000;
                width: 90%;
                max-width: 600px;
                max-height: 85vh;
                overflow: hidden;
                display: flex;
                flex-direction: column;
            }
            .preset-menu-header {
                display: flex;
                justify-content: space-between;
                align-items: center;
                padding: 20px 24px;
                border-bottom: 1px solid var(--border-color);
            }
            .preset-menu-header h3 {
                margin: 0;
                font-size: 18px;
            }
            .preset-close-btn {
                background: none;
                border: none;
                font-size: 24px;
                cursor: pointer;
                color: var(--text-secondary);
                padding: 4px 8px;
                line-height: 1;
            }
            .preset-close-btn:hover {
                color: var(--text-primary);
            }
            .preset-menu-body {
                display: flex;
                flex: 1;
                overflow: hidden;
            }
            .preset-sidebar {
                width: 180px;
                border-right: 1px solid var(--border-color);
                padding: 16px;
                background: var(--bg-tertiary);
                flex-shrink: 0;
            }
            .preset-sidebar-title {
                font-size: 11px;
                text-transform: uppercase;
                color: var(--text-muted);
                margin-bottom: 12px;
                letter-spacing: 0.5px;
            }
            .preset-board-option {
                display: flex;
                align-items: center;
                padding: 10px 12px;
                margin-bottom: 6px;
                border-radius: var(--radius);
                cursor: pointer;
                transition: background 0.15s;
            }
            .preset-board-option:hover {
                background: var(--bg-secondary);
            }
            .preset-board-option.selected {
                background: rgba(47, 129, 247, 0.15);
                border: 1px solid var(--accent-blue);
            }
            .preset-board-option input {
                margin-right: 10px;
            }
            .preset-board-option label {
                cursor: pointer;
                font-size: 14px;
            }
            .preset-pins-area {
                flex: 1;
                padding: 20px;
                overflow-y: auto;
            }
            .preset-pins-grid {
                display: grid;
                grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
                gap: 12px;
            }
            .preset-pin-card {
                background: var(--bg-tertiary);
                border: 1px solid var(--border-color);
                border-radius: var(--radius);
                padding: 16px;
                cursor: pointer;
                transition: all 0.15s;
            }
            .preset-pin-card:hover {
                border-color: var(--accent-blue);
                background: rgba(47, 129, 247, 0.08);
                transform: translateY(-2px);
            }
            .preset-pin-card.disabled {
                opacity: 0.5;
                cursor: not-allowed;
            }
            .preset-pin-card.disabled:hover {
                border-color: var(--border-color);
                background: var(--bg-tertiary);
                transform: none;
            }
            .preset-pin-number {
                font-size: 20px;
                font-weight: 700;
                color: var(--accent-blue);
                margin-bottom: 4px;
            }
            .preset-pin-label {
                font-size: 14px;
                font-weight: 500;
                color: var(--text-primary);
                margin-bottom: 4px;
            }
            .preset-pin-note {
                font-size: 12px;
                color: var(--text-muted);
            }
            .preset-pin-action {
                display: inline-block;
                font-size: 10px;
                text-transform: uppercase;
                padding: 2px 6px;
                border-radius: 3px;
                margin-top: 8px;
                background: rgba(47, 129, 247, 0.15);
                color: var(--accent-blue);
            }
            .preset-custom-section {
                margin-top: 20px;
                padding-top: 20px;
                border-top: 1px solid var(--border-color);
            }
            .preset-custom-btn {
                display: flex;
                align-items: center;
                gap: 12px;
                width: 100%;
                padding: 16px;
                background: var(--bg-tertiary);
                border: 2px dashed var(--border-color);
                border-radius: var(--radius);
                cursor: pointer;
                transition: all 0.15s;
            }
            .preset-custom-btn:hover {
                border-color: var(--accent-blue);
                background: rgba(47, 129, 247, 0.08);
            }
            .preset-custom-icon {
                font-size: 24px;
                color: var(--accent-blue);
            }
            .preset-custom-text {
                text-align: left;
            }
            .preset-custom-title {
                font-weight: 600;
                color: var(--text-primary);
            }
            .preset-custom-desc {
                font-size: 13px;
                color: var(--text-muted);
            }
        `;
        document.head.appendChild(style);
    }

    // Remove existing menu if present
    const existingMenu = document.getElementById('gpio-preset-menu');
    if (existingMenu) existingMenu.remove();
    const existingOverlay = document.getElementById('gpio-preset-overlay');
    if (existingOverlay) existingOverlay.remove();

    // Create overlay
    const overlay = document.createElement('div');
    overlay.id = 'gpio-preset-overlay';
    overlay.className = 'gpio-preset-overlay';
    overlay.onclick = closeGpioPresetMenu;
    document.body.appendChild(overlay);

    // Create menu
    const menu = document.createElement('div');
    menu.id = 'gpio-preset-menu';
    menu.className = 'gpio-preset-menu';
    menu.innerHTML = `
        <div class="preset-menu-header">
            <h3>Add GPIO Pin</h3>
            <button class="preset-close-btn" onclick="closeGpioPresetMenu()">&times;</button>
        </div>
        <div class="preset-menu-body">
            <div class="preset-sidebar">
                <div class="preset-sidebar-title">Select Board</div>
                ${Object.entries(gpioPresets).map(([key, board]) => `
                    <div class="preset-board-option ${key === selectedGpioBoard ? 'selected' : ''}"
                         onclick="selectGpioBoard('${key}')">
                        <input type="radio" name="gpio-board" value="${key}"
                               ${key === selectedGpioBoard ? 'checked' : ''}>
                        <label>${board.name}</label>
                    </div>
                `).join('')}
            </div>
            <div class="preset-pins-area">
                <div class="preset-pins-grid" id="gpio-pins-grid">
                    ${renderGpioPins(selectedGpioBoard)}
                </div>
                <div class="preset-custom-section">
                    <button class="preset-custom-btn" onclick="addGpio(); closeGpioPresetMenu();">
                        <span class="preset-custom-icon">+</span>
                        <div class="preset-custom-text">
                            <div class="preset-custom-title">Custom GPIO</div>
                            <div class="preset-custom-desc">Add a blank GPIO with manual configuration</div>
                        </div>
                    </button>
                </div>
            </div>
        </div>
    `;
    document.body.appendChild(menu);
}

function renderGpioPins(boardKey) {
    const board = gpioPresets[boardKey];
    if (!board) return '';

    return board.pins.map(p => {
        const isUsed = gpioConfig.some(g => g.pin === p.pin);
        const actionLabel = {
            'ring_button': 'Ring',
            'door_contact': 'Door',
            'generic': 'Generic'
        }[p.action] || p.action;

        return `
            <div class="preset-pin-card ${isUsed ? 'disabled' : ''}"
                 onclick="${isUsed ? '' : `addGpioFromPreset(${p.pin}, '${p.label}', '${p.action}', '${p.pullMode}'); closeGpioPresetMenu();`}"
                 title="${isUsed ? 'Already configured' : 'Click to add'}">
                <div class="preset-pin-number">GPIO ${p.pin}</div>
                <div class="preset-pin-label">${p.label}</div>
                <div class="preset-pin-note">${p.note}</div>
                <span class="preset-pin-action">${actionLabel}</span>
            </div>
        `;
    }).join('');
}

function selectGpioBoard(boardKey) {
    selectedGpioBoard = boardKey;

    // Update radio buttons and selection state
    document.querySelectorAll('.preset-board-option').forEach(opt => {
        const isSelected = opt.querySelector('input').value === boardKey;
        opt.classList.toggle('selected', isSelected);
        opt.querySelector('input').checked = isSelected;
    });

    // Update pins grid
    const grid = document.getElementById('gpio-pins-grid');
    if (grid) {
        grid.innerHTML = renderGpioPins(boardKey);
    }
}

function closeGpioPresetMenu() {
    const menu = document.getElementById('gpio-preset-menu');
    const overlay = document.getElementById('gpio-preset-overlay');
    if (menu) menu.style.display = 'none';
    if (overlay) overlay.style.display = 'none';
}

function addGpioFromPreset(pin, label, action, pullMode) {
    if (gpioConfig.length >= 8) {
        alert('Maximum 8 GPIO pins allowed');
        return;
    }

    // Check if pin is already configured
    if (gpioConfig.some(g => g.pin === pin)) {
        alert(`GPIO ${pin} is already configured`);
        return;
    }

    gpioConfig.push({
        enabled: true,
        pin: pin,
        action: action,
        pullMode: pullMode,
        label: label,
        debounceMs: 50,
        holdMs: 100
    });

    populateGpioList();
}

function addGpio() {
    if (gpioConfig.length >= 8) {
        alert('Maximum 8 GPIO pins allowed');
        return;
    }

    gpioConfig.push({
        enabled: true,
        pin: 34,
        action: 'ring_button',
        pullMode: 'up',
        label: 'New GPIO',
        debounceMs: 50,
        holdMs: 100
    });

    populateGpioList();
}

function removeGpio(index) {
    if (index >= 0 && index < gpioConfig.length) {
        gpioConfig.splice(index, 1);
        populateGpioList();
    }
}

function updateGpio(index, field, value) {
    if (index >= 0 && index < gpioConfig.length) {
        gpioConfig[index][field] = value;
        // Update the title when pin or label changes
        if (field === 'pin' || field === 'label') {
            populateGpioList();
        }
    }
}

function updateGpioHelp(index) {
    const helpDiv = document.getElementById(`gpio-help-${index}`);
    if (helpDiv && gpioConfig[index]) {
        helpDiv.innerHTML = getGpioHelpText(
            gpioConfig[index].action,
            gpioConfig[index].pullMode,
            gpioConfig[index].label
        );
    }
}

function getGpioHelpText(action, pullMode, label) {
    const pullDesc = pullMode === 'up'
        ? 'Connect between GPIO and GND. Internal pull-up resistor keeps pin HIGH when not active.'
        : 'Connect between GPIO and 3.3V. Internal pull-down resistor keeps pin LOW when not active.';

    switch (action) {
        case 'ring_button':
            return `<strong>Ring Button:</strong> Triggers a doorbell ring on your UniFi Access system when pressed. ${pullDesc}`;
        case 'door_contact':
            return `<strong>Door Contact:</strong> Dismisses an active doorbell call when triggered. Typically connected to a door position sensor. ${pullDesc}`;
        case 'generic':
            const mqttTopic = config.mqtt?.topic || 'doorbell';
            const sanitizedLabel = (label || 'gpio').toLowerCase().replace(/\\s+/g, '_');
            return `<strong>Generic (MQTT):</strong> Publishes state changes to MQTT topic <code>${mqttTopic}/gpio/${sanitizedLabel}</code>. ${pullDesc}`;
        default:
            return pullDesc;
    }
}

// =============================================================================
// MQTT Triggers Configuration
// =============================================================================

function populateMqttTriggersList() {
    const container = document.getElementById('mqtt-triggers-list');
    if (!container) return;

    container.innerHTML = '';

    if (mqttTriggers.length === 0) {
        container.innerHTML = '<p class="form-hint">No MQTT triggers configured. Add a trigger to react to MQTT messages.</p>';
        return;
    }

    mqttTriggers.forEach((trigger, index) => {
        const div = document.createElement('div');
        div.className = 'mqtt-trigger-item';
        div.innerHTML = `
            <div class="mqtt-trigger-header">
                <span class="mqtt-trigger-title">${trigger.label || 'Unnamed Trigger'}</span>
                <button class="btn btn-danger btn-sm" onclick="removeMqttTrigger(${index})">Remove</button>
            </div>
            <div class="mqtt-trigger-fields">
                <div class="form-group">
                    <label>Label</label>
                    <input type="text" value="${trigger.label || ''}" placeholder="Front Door Contact"
                           onchange="updateMqttTrigger(${index}, 'label', this.value)">
                </div>
                <div class="form-group">
                    <label>Topic</label>
                    <input type="text" value="${trigger.topic || ''}" placeholder="zigbee2mqtt/door_sensor"
                           onchange="updateMqttTrigger(${index}, 'topic', this.value)">
                </div>
                <div class="form-group">
                    <label>JSON Field</label>
                    <input type="text" value="${trigger.jsonField || ''}" placeholder="contact"
                           onchange="updateMqttTrigger(${index}, 'jsonField', this.value)">
                </div>
                <div class="form-group">
                    <label>Trigger Value</label>
                    <input type="text" value="${trigger.triggerValue || ''}" placeholder="false"
                           onchange="updateMqttTrigger(${index}, 'triggerValue', this.value)">
                </div>
                <div class="form-group">
                    <label>Action</label>
                    <select onchange="updateMqttTrigger(${index}, 'action', this.value)">
                        <option value="dismiss" ${trigger.action === 'dismiss' ? 'selected' : ''}>Dismiss Call</option>
                        <option value="ring" ${trigger.action === 'ring' ? 'selected' : ''}>Ring Doorbell</option>
                    </select>
                </div>
            </div>
            <div class="mqtt-trigger-help">
                ${getMqttTriggerHelpText(trigger)}
            </div>
        `;
        container.appendChild(div);
    });

    // Add styles if not present
    if (!document.getElementById('mqtt-trigger-styles')) {
        const style = document.createElement('style');
        style.id = 'mqtt-trigger-styles';
        style.textContent = `
            .mqtt-trigger-item {
                background: var(--bg-tertiary);
                border: 1px solid var(--border-color);
                border-radius: var(--radius);
                padding: 16px;
                margin-bottom: 12px;
            }
            .mqtt-trigger-header {
                display: flex;
                justify-content: space-between;
                align-items: center;
                margin-bottom: 12px;
            }
            .mqtt-trigger-title {
                font-weight: 600;
            }
            .mqtt-trigger-fields {
                display: grid;
                grid-template-columns: repeat(5, 1fr);
                gap: 12px;
                align-items: end;
            }
            .mqtt-trigger-fields .form-group {
                margin-bottom: 0;
            }
            .mqtt-trigger-fields input,
            .mqtt-trigger-fields select {
                height: 40px;
            }
            @media (max-width: 900px) {
                .mqtt-trigger-fields {
                    grid-template-columns: repeat(2, 1fr);
                }
            }
            @media (max-width: 480px) {
                .mqtt-trigger-fields {
                    grid-template-columns: 1fr;
                }
            }
            .mqtt-trigger-help {
                margin-top: 12px;
                padding: 12px;
                background: rgba(47, 129, 247, 0.1);
                border-radius: var(--radius);
                font-size: 13px;
                color: var(--text-secondary);
            }
        `;
        document.head.appendChild(style);
    }
}

function addMqttTrigger() {
    if (mqttTriggers.length >= 4) {
        alert('Maximum 4 MQTT triggers allowed');
        return;
    }

    mqttTriggers.push({
        enabled: true,
        topic: '',
        jsonField: 'contact',
        triggerValue: 'false',
        action: 'dismiss',
        label: 'New Trigger'
    });

    populateMqttTriggersList();
}

function removeMqttTrigger(index) {
    if (index >= 0 && index < mqttTriggers.length) {
        mqttTriggers.splice(index, 1);
        populateMqttTriggersList();
    }
}

function updateMqttTrigger(index, field, value) {
    if (index >= 0 && index < mqttTriggers.length) {
        mqttTriggers[index][field] = value;
        // Update the title when label changes
        if (field === 'label') {
            populateMqttTriggersList();
        }
    }
}

function getMqttTriggerHelpText(trigger) {
    const actionText = trigger.action === 'ring'
        ? 'trigger a <strong>doorbell ring</strong>'
        : 'dismiss the <strong>active call</strong>';

    if (!trigger.topic || !trigger.jsonField) {
        return 'Configure the topic and JSON field to subscribe to.';
    }

    return `When <code>${trigger.topic}</code> receives a message with <code>"${trigger.jsonField}": ${trigger.triggerValue}</code>, this will ${actionText}.`;
}

// =============================================================================
// Settings
// =============================================================================

async function saveSettings() {
    const configUpdate = {
        network: {
            wifiSsid: document.getElementById('wifi-ssid').value.trim(),
            wifiPassword: document.getElementById('wifi-password').value
        },
        mqtt: {
            enabled: document.getElementById('mqtt-enabled').checked,
            server: document.getElementById('mqtt-server').value.trim(),
            port: parseInt(document.getElementById('mqtt-port').value, 10) || 1883,
            topic: document.getElementById('mqtt-topic').value.trim(),
            authEnabled: document.getElementById('mqtt-auth').checked,
            username: document.getElementById('mqtt-username').value.trim(),
            password: document.getElementById('mqtt-password').value
        },
        web: {
            username: document.getElementById('web-username').value.trim(),
            password: document.getElementById('web-password').value
        },
        gpios: gpioConfig,
        mqttTriggers: mqttTriggers
    };

    try {
        const response = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(configUpdate)
        });

        const result = await response.json();
        if (result.success) {
            alert('Settings saved! Reboot to apply changes.');
        } else {
            alert('Failed to save: ' + (result.message || 'Unknown error'));
        }
    } catch (e) {
        alert('Error saving settings: ' + e.message);
    }
}

// =============================================================================
// Control Actions
// =============================================================================

async function triggerRing() {
    try {
        const response = await fetch('/api/control/ring', { method: 'POST' });
        const result = await response.json();
        if (!result.success) {
            alert('Failed to trigger ring: ' + (result.message || 'Unknown error'));
        }
    } catch (e) {
        alert('Error: ' + e.message);
    }
}

async function dismissCall() {
    try {
        const response = await fetch('/api/control/dismiss', { method: 'POST' });
        const result = await response.json();
        if (!result.success) {
            alert('Failed to dismiss: ' + (result.message || 'Unknown error'));
        }
    } catch (e) {
        alert('Error: ' + e.message);
    }
}

async function rebootDevice() {
    if (!confirm('Reboot the device?')) return;
    try {
        await fetch('/api/control/reboot', { method: 'POST' });
        alert('Device is rebooting...');
    } catch (e) {}
}

async function resetConfig() {
    if (!confirm('This will erase all configuration and reboot. Continue?')) return;
    if (!confirm('Are you sure? This cannot be undone!')) return;
    try {
        await fetch('/api/control/reset', { method: 'POST' });
        alert('Configuration reset. Device is rebooting...');
    } catch (e) {}
}

// =============================================================================
// Firmware Update
// =============================================================================

async function uploadFirmware() {
    const fileInput = document.getElementById('firmware-file');
    const file = fileInput.files[0];

    if (!file) {
        alert('Please select a firmware file');
        return;
    }

    if (!file.name.endsWith('.bin')) {
        alert('Please select a .bin file');
        return;
    }

    if (!confirm('Upload firmware and update? The device will reboot after update.')) {
        return;
    }

    const progressContainer = document.getElementById('upload-progress');
    const progressFill = document.getElementById('progress-fill');
    const progressText = document.getElementById('progress-text');

    progressContainer.classList.remove('hidden');

    try {
        const formData = new FormData();
        formData.append('firmware', file);

        const xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/ota/upload', true);

        xhr.upload.onprogress = (e) => {
            if (e.lengthComputable) {
                const percent = Math.round((e.loaded / e.total) * 100);
                progressFill.style.width = percent + '%';
                progressText.textContent = `Uploading: ${percent}%`;
            }
        };

        xhr.onload = () => {
            if (xhr.status === 200) {
                progressText.textContent = 'Update complete! Rebooting...';
                setTimeout(() => location.reload(), 10000);
            } else {
                try {
                    const result = JSON.parse(xhr.responseText);
                    alert('Update failed: ' + (result.message || 'Unknown error'));
                } catch (e) {
                    alert('Update failed: ' + xhr.statusText);
                }
                progressContainer.classList.add('hidden');
            }
        };

        xhr.onerror = () => {
            alert('Upload failed. Check connection.');
            progressContainer.classList.add('hidden');
        };

        xhr.send(formData);
    } catch (e) {
        alert('Error: ' + e.message);
        progressContainer.classList.add('hidden');
    }
}
