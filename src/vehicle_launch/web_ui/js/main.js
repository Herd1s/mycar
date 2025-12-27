// --- Configuration ---
const CONFIG = {
    rosUrl: 'ws://' + window.location.hostname + ':9090',
    topics: {
        cmdVel: '/cmd_vel',
        feedback: '/stm32_feedback'
    },
    services: {
        listFiles: '/get_pcd_files',
        uploadFile: '/upload_pcd_file'
    },
    joystick: {
        maxDistance: 70 // Pixels
    }
};

// --- State ---
const state = {
    maxLinear: 0.5,
    maxAngular: 1.0,
    connected: false,
    selectedFile: null
};

// --- UI Elements ---
const ui = {
    status: document.getElementById('connection-status'),
    joystickBase: document.getElementById('joystick-base'),
    joystickKnob: document.getElementById('joystick-knob'),
    cmdLinear: document.getElementById('cmd-linear'),
    cmdAngular: document.getElementById('cmd-angular'),
    stm32X: document.getElementById('stm32-x'),
    stm32Y: document.getElementById('stm32-y'),
    logBox: document.getElementById('system-log'),
    sliderLin: document.getElementById('slider-max-lin'),
    sliderAng: document.getElementById('slider-max-ang'),
    dispLin: document.getElementById('disp-max-lin'),
    dispAng: document.getElementById('disp-max-ang'),
    // File Upload UI
    fileList: document.getElementById('file-list-container'),
    btnRefresh: document.getElementById('btn-refresh-files'),
    btnUpload: document.getElementById('btn-upload-file')
};

// --- Logger ---
const Logger = {
    log: (msg, type = 'info') => {
        const entry = document.createElement('div');
        entry.className = `log-entry ${type}`;
        const time = new Date().toLocaleTimeString();
        entry.textContent = `[${time}] ${msg}`;
        ui.logBox.appendChild(entry);
        ui.logBox.scrollTop = ui.logBox.scrollHeight;
        
        // Limit log size
        if (ui.logBox.children.length > 100) {
            ui.logBox.removeChild(ui.logBox.firstChild);
        }
    },
    error: (msg) => Logger.log(msg, 'error'),
    warn: (msg) => Logger.log(msg, 'warn')
};

// --- ROS Manager ---
class RosManager {
    constructor() {
        this.ros = new ROSLIB.Ros({ url: CONFIG.rosUrl });
        this.setupEvents();
        this.setupTopics();
        this.setupServices();
    }

    setupEvents() {
        this.ros.on('connection', () => {
            state.connected = true;
            ui.status.textContent = 'Connected';
            ui.status.className = 'status connected';
            Logger.log('Connected to ROS Bridge');
        });

        this.ros.on('error', (error) => {
            ui.status.textContent = 'Error';
            ui.status.className = 'status error';
            Logger.error('Connection error');
        });

        this.ros.on('close', () => {
            state.connected = false;
            ui.status.textContent = 'Disconnected';
            ui.status.className = 'status disconnected';
            Logger.warn('Connection closed');
        });
    }

    setupTopics() {
        // Publisher
        this.cmdVel = new ROSLIB.Topic({
            ros: this.ros,
            name: CONFIG.topics.cmdVel,
            messageType: 'geometry_msgs/Twist'
        });

        // Subscriber
        this.feedback = new ROSLIB.Topic({
            ros: this.ros,
            name: CONFIG.topics.feedback,
            messageType: 'geometry_msgs/Point'
        });

        this.feedback.subscribe((msg) => {
            ui.stm32X.textContent = msg.x.toFixed(3);
            ui.stm32Y.textContent = msg.y.toFixed(3);
        });
    }

    setupServices() {
        this.listFilesClient = new ROSLIB.Service({
            ros: this.ros,
            name: CONFIG.services.listFiles,
            serviceType: 'cloud_uploader/GetFileList'
        });

        this.uploadFileClient = new ROSLIB.Service({
            ros: this.ros,
            name: CONFIG.services.uploadFile,
            serviceType: 'cloud_uploader/UploadFile'
        });
    }

    publishCmd(linear, angular) {
        if (!state.connected) return;
        
        const twist = new ROSLIB.Message({
            linear: { x: linear, y: 0, z: 0 },
            angular: { x: 0, y: 0, z: angular }
        });
        this.cmdVel.publish(twist);
        
        // Update UI
        ui.cmdLinear.textContent = linear.toFixed(2);
        ui.cmdAngular.textContent = angular.toFixed(2);

        // Log control data
        Logger.log(`Cmd: Lin=${linear.toFixed(2)}, Ang=${angular.toFixed(2)}`);
    }

    getFileList() {
        if (!state.connected) {
            Logger.error("Not connected to ROS");
            return;
        }
        
        const request = new ROSLIB.ServiceRequest({});
        Logger.log("Fetching file list...");
        
        this.listFilesClient.callService(request, (result) => {
            Logger.log(`Found ${result.files.length} files.`);
            this.renderFileList(result.files);
        }, (error) => {
            Logger.error("Failed to get file list: " + error);
        });
    }

    uploadFile(filename) {
        if (!state.connected) return;
        
        const request = new ROSLIB.ServiceRequest({ filename: filename });
        Logger.log(`Uploading ${filename}...`);
        
        ui.btnUpload.disabled = true;
        ui.btnUpload.textContent = "Uploading...";

        this.uploadFileClient.callService(request, (result) => {
            if (result.success) {
                Logger.log(`Upload Success: ${result.message}`, 'success');
            } else {
                Logger.error(`Upload Failed: ${result.message}`);
            }
            ui.btnUpload.disabled = false;
            ui.btnUpload.textContent = "Upload Selected";
        }, (error) => {
            Logger.error("Service call failed: " + error);
            ui.btnUpload.disabled = false;
            ui.btnUpload.textContent = "Upload Selected";
        });
    }

    renderFileList(files) {
        ui.fileList.innerHTML = '';
        if (files.length === 0) {
            ui.fileList.innerHTML = '<div class="empty-state">No files found</div>';
            return;
        }

        files.forEach(file => {
            const div = document.createElement('div');
            div.className = 'file-item';
            div.textContent = file;
            div.onclick = () => {
                // Deselect others
                document.querySelectorAll('.file-item').forEach(el => el.classList.remove('selected'));
                div.classList.add('selected');
                state.selectedFile = file;
                ui.btnUpload.disabled = false;
            };
            ui.fileList.appendChild(div);
        });
    }
}

// --- Joystick Controller ---
class JoystickController {
    constructor(rosManager) {
        this.ros = rosManager;
        this.dragging = false;
        this.setupListeners();
    }

    setupListeners() {
        const start = (e) => this.handleStart(e);
        const move = (e) => this.handleMove(e);
        const end = (e) => this.handleEnd(e);

        ui.joystickBase.addEventListener('mousedown', start);
        document.addEventListener('mousemove', move);
        document.addEventListener('mouseup', end);

        ui.joystickBase.addEventListener('touchstart', start, { passive: false });
        document.addEventListener('touchmove', move, { passive: false });
        document.addEventListener('touchend', end);
    }

    handleStart(e) {
        this.dragging = true;
        ui.joystickKnob.classList.add('dragging');
        if (e.type === 'touchstart') e.preventDefault();
        this.update(e);
    }

    handleMove(e) {
        if (!this.dragging) return;
        if (e.type === 'touchmove') e.preventDefault();
        this.update(e);
    }

    handleEnd() {
        if (!this.dragging) return;
        this.dragging = false;
        ui.joystickKnob.classList.remove('dragging');
        ui.joystickKnob.style.transform = `translate(-50%, -50%)`;
        this.ros.publishCmd(0, 0);
        Logger.log('Stopped');
    }

    update(e) {
        const rect = ui.joystickBase.getBoundingClientRect();
        const centerX = rect.left + rect.width / 2;
        const centerY = rect.top + rect.height / 2;
        
        const clientX = e.touches ? e.touches[0].clientX : e.clientX;
        const clientY = e.touches ? e.touches[0].clientY : e.clientY;
        
        let dx = clientX - centerX;
        let dy = clientY - centerY;
        
        const distance = Math.sqrt(dx*dx + dy*dy);
        const maxDist = CONFIG.joystick.maxDistance;

        if (distance > maxDist) {
            const angle = Math.atan2(dy, dx);
            dx = Math.cos(angle) * maxDist;
            dy = Math.sin(angle) * maxDist;
        }

        // Update UI
        ui.joystickKnob.style.transform = `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;

        // Calculate speeds
        // Up (negative dy) -> Positive Linear
        // Left (negative dx) -> Positive Angular
        const normX = dx / maxDist;
        const normY = -dy / maxDist;

        const linear = normY * state.maxLinear;
        const angular = -normX * state.maxAngular;

        this.ros.publishCmd(linear, angular);
    }
}

// --- Initialization ---
function init() {
    const rosManager = new RosManager();
    new JoystickController(rosManager);

    // Slider Events
    ui.sliderLin.addEventListener('input', (e) => {
        state.maxLinear = parseFloat(e.target.value);
        ui.dispLin.textContent = state.maxLinear;
    });

    ui.sliderAng.addEventListener('input', (e) => {
        state.maxAngular = parseFloat(e.target.value);
        ui.dispAng.textContent = state.maxAngular;
    });

    // File Upload Events
    ui.btnRefresh.addEventListener('click', () => {
        rosManager.getFileList();
    });

    ui.btnUpload.addEventListener('click', () => {
        if (state.selectedFile) {
            rosManager.uploadFile(state.selectedFile);
        }
    });

    Logger.log('System initialized');
}

window.addEventListener('load', init);
