/**
 * @file record_ui.js
 * @brief Autonomous Ultra-HD UI Capture Pipeline for QNX-ChaosPulse Demo Video
 *
 * Launches Chromium via Puppeteer at 3840x2160 UHD resolution, connects to the
 * live QNX-ChaosPulse Cockpit on http://localhost:3000, and autonomously executes
 * the 5-scene scenario synchronized to the exact neural voiceover track timestamps.
 *
 * Copyright (C) 2026 QNX-ChaosPulse Architects. MIT License.
 */

const puppeteer = require('puppeteer');
const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');

const PROJECT_ROOT = path.dirname(__dirname);
const CAPTURES_DIR = path.join(PROJECT_ROOT, 'media', 'captures');
const RAW_VIDEO_PATH = path.join(CAPTURES_DIR, 'raw_screen.mp4');
const FFMPEG_BIN = 'C:\\Users\\WALTON\\.gemini\\antigravity\\bin\\ffmpeg.exe';
const APP_URL = 'http://localhost:3000';

/* Scene Timestamps (exact match with neural voiceover durations) */
const TIMINGS = {
    SCENE1_END: 36.8,   /* Intro & Cockpit Architecture Overview */
    SCENE2_END: 71.7,   /* Baseline Metrics & Scorecard Deep Dive */
    SCENE3_END: 117.4,  /* CPU Saturation Storm & Priority Inversion PIP Recovery */
    SCENE4_END: 146.1,  /* Interactive Codelab Drawer & ASIL-D Audit Dossier */
    SCENE5_END: 172.8   /* Enterprise Mission, Kernel Tracing & Outro */
};

const TOTAL_DURATION_SEC = TIMINGS.SCENE5_END;

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function main() {
    console.log('===================================================================');
    console.log('  QNX-CHAOSPULSE // Autonomous Ultra-HD 60 FPS Capture Pipeline');
    console.log(`  Target URL: ${APP_URL}`);
    console.log(`  Total Planned Duration: ${TOTAL_DURATION_SEC} seconds (2.88 minutes)`);
    console.log(`  Output Path: ${RAW_VIDEO_PATH}`);
    console.log('===================================================================');

    if (!fs.existsSync(CAPTURES_DIR)) {
        fs.mkdirSync(CAPTURES_DIR, { recursive: true });
    }

    /* Spawn FFmpeg process to consume JPEG screencast frames from stdin */
    const ffmpeg = spawn(FFMPEG_BIN, [
        '-y',
        '-f', 'image2pipe',
        '-vcodec', 'mjpeg',
        '-r', '30',
        '-i', '-',
        '-c:v', 'libx264',
        '-preset', 'fast',
        '-crf', '17',
        '-pix_fmt', 'yuv420p',
        '-r', '60',
        '-vf', 'scale=3840:2160:flags=lanczos',
        RAW_VIDEO_PATH
    ]);

    let frameCount = 0;
    let isRecording = true;

    ffmpeg.stderr.on('data', (d) => {
        /* Filter verbose ffmpeg logs */
    });

    ffmpeg.on('error', (err) => {
        console.error('[FFMPEG ERROR]', err.message);
    });

    console.log('[CAPTURE] Launching Chromium in Ultra-HD viewport...');
    const browser = await puppeteer.launch({
        headless: 'new',
        args: [
            '--no-sandbox',
            '--disable-setuid-sandbox',
            '--window-size=1920,1080',
            '--disable-dev-shm-usage',
            '--autoplay-policy=no-user-gesture-required'
        ]
    });

    const page = await browser.newPage();
    await page.setViewport({
        width: 1920,
        height: 1080,
        deviceScaleFactor: 2 /* Renders crisp 3840x2160 Retina graphics */
    });

    console.log(`[CAPTURE] Connecting to cockpit at ${APP_URL}...`);
    await page.goto(APP_URL, { waitUntil: 'networkidle2' });
    await sleep(1500);

    /* Initialize Chrome DevTools Protocol Screencast session */
    const client = await page.target().createCDPSession();
    client.on('Page.screencastFrame', async ({ data, sessionId }) => {
        if (isRecording && ffmpeg.stdin.writable) {
            try {
                ffmpeg.stdin.write(Buffer.from(data, 'base64'));
                frameCount++;
                await client.send('Page.screencastFrameAck', { sessionId });
            } catch (e) {
                /* Handled stream closure */
            }
        }
    });

    await client.send('Page.startScreencast', {
        format: 'jpeg',
        quality: 90,
        everyNthFrame: 1
    });

    console.log('[CAPTURE] Screencast stream ENGAGED. Commencing scene choreography...');
    const startTime = Date.now();

    const getElapsed = () => (Date.now() - startTime) / 1000;

    /* Status logging interval */
    const logInterval = setInterval(() => {
        const elapsed = getElapsed();
        console.log(`[CAPTURE PROGRESS] Elapsed: ${elapsed.toFixed(1)}s / ${TOTAL_DURATION_SEC}s | Frames: ${frameCount}`);
    }, 10000);

    /* -------------------------------------------------------------------------
     * SCENE 1 (0.0s - 36.8s): Full Oscilloscope & Nominal Determinism
     * ------------------------------------------------------------------------- */
    console.log('[SCENE 1] Displaying wide cockpit HUD & nominal 60 FPS oscilloscope traces...');
    
    /* Toggle timebase at 18s to showcase oscilloscope flexibility */
    while (getElapsed() < 18.0) await sleep(250);
    console.log('[SCENE 1] Toggling Oscilloscope Timebase to 2X...');
    await page.evaluate(() => {
        const btn2x = document.querySelector('.timebase-btn[data-scale="2"]');
        if (btn2x) btn2x.click();
    });

    while (getElapsed() < 28.0) await sleep(250);
    console.log('[SCENE 1] Returning Oscilloscope Timebase to 1X...');
    await page.evaluate(() => {
        const btn1x = document.querySelector('.timebase-btn[data-scale="1"]');
        if (btn1x) btn1x.click();
    });

    while (getElapsed() < TIMINGS.SCENE1_END) await sleep(250);

    /* -------------------------------------------------------------------------
     * SCENE 2 (36.8s - 71.7s): Telemetry Scorecard & Priority 63 Deep Dive
     * ------------------------------------------------------------------------- */
    console.log('[SCENE 2] Smoothly scrolling focus to ASIL-D Scorecard and Priority 63 metrics...');
    await page.evaluate(() => {
        const panel = document.querySelector('.scorecard-panel');
        if (panel) panel.scrollIntoView({ behavior: 'smooth', block: 'center' });
    });

    while (getElapsed() < 55.0) await sleep(250);

    /* Showcase preset nominal verification click */
    console.log('[SCENE 2] Demonstrating Preset 1 (Nominal Avionics Loop)...');
    await page.evaluate(() => {
        const p1 = document.getElementById('presetNominal');
        if (p1) p1.click();
    });

    while (getElapsed() < TIMINGS.SCENE2_END) await sleep(250);

    /* -------------------------------------------------------------------------
     * SCENE 3 (71.7s - 117.4s): CPU Saturation Storm & Priority Inversion PIP
     * ------------------------------------------------------------------------- */
    console.log('[SCENE 3] Triggering CPU Core Saturation Storm (16 workers)...');
    await page.evaluate(() => {
        window.scrollTo({ top: 0, behavior: 'smooth' });
    });
    await sleep(1000);

    /* Set storm workers to 16 and engage toggle */
    await page.evaluate(() => {
        const range = document.getElementById('rangeStormThreads');
        if (range) {
            range.value = 16;
            range.dispatchEvent(new Event('input'));
        }
        const toggle = document.getElementById('toggleCpuStorm');
        if (toggle && !toggle.checked) toggle.click();
    });

    console.log('[SCENE 3] CPU Storm active. Background cores saturated at 98% load.');
    while (getElapsed() < 94.0) await sleep(250);

    /* Inject Microsecond Priority Inversion Trap */
    console.log('[SCENE 3] Injecting Microsecond Priority Inversion Trap...');
    await page.evaluate(() => {
        const btn = document.getElementById('btnTriggerInversion');
        if (btn) btn.click();
    });

    /* Allow the 6.8 us spike, ASIL-D breach alert, and 300 ms PIP auto-heal to be recorded */
    while (getElapsed() < 108.0) await sleep(250);

    /* Reset to nominal state */
    console.log('[SCENE 3] Executing Panic Reset to restore baseline...');
    await page.evaluate(() => {
        const resetBtn = document.getElementById('btnPanicReset');
        if (resetBtn) resetBtn.click();
    });

    while (getElapsed() < TIMINGS.SCENE3_END) await sleep(250);

    /* -------------------------------------------------------------------------
     * SCENE 4 (117.4s - 146.1s): Interactive Codelab Drawer & ASIL-D Audit Dossier
     * ------------------------------------------------------------------------- */
    console.log('[SCENE 4] Opening Interactive QNX 8.0 Developer Codelab...');
    await page.evaluate(() => {
        const codelabBtn = document.getElementById('btnOpenCodelab');
        if (codelabBtn) codelabBtn.click();
    });
    await sleep(2000);

    /* Cycle through Codelab tabs */
    console.log('[SCENE 4] Navigating Codelab Module 2: TSC Calibration...');
    await page.evaluate(() => {
        const tab2 = document.querySelector('.codelab-tab-btn[data-tab="module2"]');
        if (tab2) tab2.click();
    });
    await sleep(3500);

    console.log('[SCENE 4] Navigating Codelab Module 3: SCHED_FIFO Storm Isolation...');
    await page.evaluate(() => {
        const tab3 = document.querySelector('.codelab-tab-btn[data-tab="module3"]');
        if (tab3) tab3.click();
    });
    await sleep(3500);

    console.log('[SCENE 4] Navigating Codelab Module 4: Priority Inheritance Protocol...');
    await page.evaluate(() => {
        const tab4 = document.querySelector('.codelab-tab-btn[data-tab="module4"]');
        if (tab4) tab4.click();
    });
    await sleep(3500);

    /* Close Codelab Modal */
    console.log('[SCENE 4] Closing Codelab and opening ASIL-D Audit Report Exporter...');
    await page.evaluate(() => {
        const closeBtn = document.getElementById('btnCloseCodelab');
        if (closeBtn) closeBtn.click();
    });
    await sleep(1000);

    /* Open ASIL-D Audit Dossier */
    await page.evaluate(() => {
        const auditBtn = document.getElementById('btnExportAudit');
        if (auditBtn) auditBtn.click();
    });
    await sleep(6000);

    /* Close Audit Modal */
    await page.evaluate(() => {
        const closeAudit = document.getElementById('btnCloseAudit');
        if (closeAudit) closeAudit.click();
    });

    while (getElapsed() < TIMINGS.SCENE4_END) await sleep(250);

    /* -------------------------------------------------------------------------
     * SCENE 5 (146.1s - 172.8s): Outro & Steady-State Verification
     * ------------------------------------------------------------------------- */
    console.log('[SCENE 5] Returning to wide cockpit view for closing summary...');
    await page.evaluate(() => {
        window.scrollTo({ top: 0, behavior: 'smooth' });
    });

    while (getElapsed() < TOTAL_DURATION_SEC) await sleep(250);

    clearInterval(logInterval);
    const finalElapsed = getElapsed();
    console.log(`[CAPTURE] Reached target duration: ${finalElapsed.toFixed(2)}s. Stopping screencast...`);

    isRecording = false;
    await client.send('Page.stopScreencast');
    ffmpeg.stdin.end();

    console.log('[CAPTURE] Waiting for FFmpeg to finalize raw UHD stream...');
    await new Promise((resolve) => {
        ffmpeg.on('close', resolve);
    });

    await browser.close();

    const stats = fs.statSync(RAW_VIDEO_PATH);
    const sizeMb = (stats.size / (1024 * 1024)).toFixed(2);
    console.log('===================================================================');
    console.log(`  UI CAPTURE COMPLETE!`);
    console.log(`  Output: ${RAW_VIDEO_PATH}`);
    console.log(`  Frames Captured: ${frameCount} | Size: ${sizeMb} MB`);
    console.log('===================================================================');
}

main().catch((err) => {
    console.error('[FATAL CAPTURE ERROR]', err);
    process.exit(1);
});
