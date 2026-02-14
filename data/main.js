// Jingle Machine Settings - JavaScript

// Load config on page load
window.addEventListener('DOMContentLoaded', async () => {
    await loadConfig();
});

// Load configuration from ESP32
async function loadConfig() {
    try {
        const response = await fetch('/api/config');
        const config = await response.json();

        // Populate Bluetooth settings
        document.getElementById('btDevice').value = config.btDevice || 'JBL Flip 5';
        document.getElementById('btVolume').value = config.btVolume || 80;

        console.log('Config loaded:', config);
    } catch (error) {
        console.error('Failed to load config:', error);
        showStatus('Failed to load configuration', 'error');
    }
}

// Save configuration to ESP32
async function saveConfig() {
    const config = {
        btDevice: document.getElementById('btDevice').value,
        btVolume: parseInt(document.getElementById('btVolume').value),
        buttons: [] // TODO: Add button configuration
    };

    try {
        const response = await fetch('/api/config', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(config)
        });

        if (response.ok) {
            showStatus('Configuration saved successfully!', 'success');
            console.log('Config saved:', config);
        } else {
            showStatus('Failed to save configuration', 'error');
        }
    } catch (error) {
        console.error('Error saving config:', error);
        showStatus('Error saving configuration', 'error');
    }
}

// Exit Settings Mode
async function exitSettings() {
    if (!confirm('Exit Settings Mode and return to Normal Mode?\n\nThe ESP32 will restart.')) {
        return;
    }

    try {
        await fetch('/api/exit', { method: 'POST' });
        showStatus('Restarting in Normal Mode...', 'success');

        setTimeout(() => {
            window.location.href = '/';
        }, 3000);
    } catch (error) {
        // Expected error as device restarts
        console.log('Device restarting...');
    }
}

// Show status message
function showStatus(message, type) {
    const statusDiv = document.getElementById('status');
    statusDiv.textContent = message;
    statusDiv.className = 'status ' + type;

    // Hide after 5 seconds
    setTimeout(() => {
        statusDiv.style.display = 'none';
    }, 5000);
}
