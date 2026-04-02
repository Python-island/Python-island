/* ─── Public API (called by Python via QWebChannel) ─────────────────────── */

function updateTime(t) {
    document.getElementById('time').textContent = t;
}

function setWebState(state) {
    const island = document.getElementById('island');
    island.classList.remove('active', 'clicked');
    if (state === true) {
        island.classList.add('active');
    }
}

function updateSystemStatus(data) {
    document.getElementById('wifi-wrapper').className =
        'icon-wrapper ' + (data.wifi === 'online' ? 'status-green' : 'status-off');
    document.getElementById('bluetooth-wrapper').className =
        'icon-wrapper ' + (data.bluetooth.status === 'on' ? 'status-green' : 'status-off');

    const batLevel = data.battery.level || 0;
    const batStatus = data.battery.status || '';
    const batWrap = document.getElementById('battery-wrapper');

    if (batStatus.includes('充电') || batStatus.includes('接通')) {
        batWrap.className = 'icon-wrapper status-green';
    } else {
        batWrap.className = 'icon-wrapper ' + (batLevel <= 20 ? 'status-red' : 'status-green');
    }

    const wifiStr = data.wifi === 'online' ? '已连接至互联网' : '未连接至互联网';

    let btStr = '未连接设备';
    if (data.bluetooth.status === 'on' && data.bluetooth.devices && data.bluetooth.devices.length > 0) {
        btStr = data.bluetooth.devices.length === 1
            ? data.bluetooth.devices[0]
            : `${data.bluetooth.devices.length} 个设备`;
    }

    const batStr = `电池 ${batLevel}%`;

    document.getElementById('status-info').innerHTML =
        `<div style="display:flex;align-items:center;gap:15px;">
            <div style="display:flex;align-items:center;gap:8px;">
                <img src="public/image/wifi.png" class="icon" style="width:14px;height:14px;opacity:0.7;" />
                <span class="highlight">${wifiStr}</span>
            </div>
            <div style="display:flex;align-items:center;gap:8px;width:80px;">
                <img src="public/image/bluetooth.png" class="icon" style="width:14px;height:14px;opacity:0.7;flex-shrink:0;" />
                <span class="highlight" style="white-space:nowrap;overflow:hidden;text-overflow:ellipsis;flex-shrink:1;">${btStr}</span>
            </div>
            <div style="display:flex;align-items:center;gap:8px;">
                <img src="public/image/battery.png" class="icon" style="width:14px;height:14px;opacity:0.7;" />
                <span class="highlight">${batStr}</span>
            </div>
         </div>`;
}

function showNotification(title, message) {
    let notification = document.getElementById('notification');
    if (!notification) {
        notification = document.createElement('div');
        notification.id = 'notification';
        notification.className = 'notification';
        document.getElementById('island').appendChild(notification);
    }
    notification.innerHTML = `<strong>${title}</strong><br>${message}`;
    notification.classList.add('show');
    setTimeout(() => notification.classList.remove('show'), 3000);
}

/* ─── Internal bridge ───────────────────────────────────────────────────── */

var pyisland; // exposed to Python

document.addEventListener('DOMContentLoaded', function() {
    new QWebChannel(qt.webChannelTransport, function(channel) {
        pyisland = channel.objects.pyisland;
        bindIconClicks();
    });
});

/**
 * Rebind click handlers to open Windows Settings.
 * Called once after QWebChannel is ready; safe to re-call if needed.
 */
function bindIconClicks() {
    const open = (setting) => {
        if (pyisland) pyisland.openWindowsSettings(setting);
    };
    document.getElementById('wifi-wrapper').addEventListener('click', () => open('network'));
    document.getElementById('bluetooth-wrapper').addEventListener('click', () => open('bluetooth'));
    document.getElementById('battery-wrapper').addEventListener('click', () => open('battery'));
    document.getElementById('notice-wrapper').addEventListener('click', () => open('notifications'));
}

// 添加点击展开功能
document.addEventListener('DOMContentLoaded', function() {
    const island = document.getElementById('island');
    let isClickedExpanded = false;
    
    island.addEventListener('click', function(e) {
        // 检查点击目标是否是按钮，如果是则不执行展开/收起
        if (e.target.closest('.icon-wrapper')) {
            return;
        }
        
        isClickedExpanded = !isClickedExpanded;
        if (isClickedExpanded) {
            // 点击展开
            island.classList.remove('active');
            island.classList.add('clicked');
        } else {
            // 点击收起
            island.classList.remove('active', 'clicked');
        }
    });
});