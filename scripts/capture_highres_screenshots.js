/**
 * @file capture_highres_screenshots.js
 * @brief Autonomous 4K Ultra-HD Screenshot Capture Pipeline for Devpost Gallery
 *
 * Captures 6 production-grade, 3840x2160 Ultra-HD PNG screenshots of the
 * QNX-ChaosPulse Web Cockpit (http://localhost:3000) using Puppeteer.
 *
 * Copyright (C) 2026 QNX-ChaosPulse Architects. MIT License.
 */

const puppeteer = require('puppeteer');
const path = require('path');
const fs = require('fs');

const PROJECT_ROOT = path.dirname(__dirname);
const SCREENSHOTS_DIR = path.join(PROJECT_ROOT, 'docs', 'screenshots');
const APP_URL = 'http://localhost:3000';

function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function main() {
    console.log('===================================================================');
    console.log('  QNX-CHAOSPULSE // 4K Ultra-HD Production Screenshot Pipeline');
    console.log(`  Target URL: ${APP_URL}`);
    console.log(`  Output Directory: ${SCREENSHOTS_DIR}`);
    console.log('===================================================================');

    if (!fs.existsSync(SCREENSHOTS_DIR)) {
        fs.mkdirSync(SCREENSHOTS_DIR, { recursive: true });
    }

    console.log('[HEADLESS] Launching Chromium with 4K deviceScaleFactor: 2...');
    const browser = await puppeteer.launch({
        headless: 'new',
        args: [
            '--no-sandbox',
            '--disable-setuid-sandbox',
            '--window-size=1920,1080',
            '--disable-dev-shm-usage'
        ]
    });

    const page = await browser.newPage();
    await page.setViewport({
        width: 1920,
        height: 1080,
        deviceScaleFactor: 2 /* Produces 3840x2160 Ultra-HD resolution */
    });

    console.log(`[HEADLESS] Navigating to ${APP_URL}...`);
    await page.goto(APP_URL, { waitUntil: 'networkidle2' });

    /* Wait for WebSocket connection and live telemetry initialization */
    console.log('[HEADLESS] Waiting for WebSocket telemetry connection...');
    await page.waitForFunction(() => {
        const wsEl = document.getElementById('wsStatusText');
        return wsEl && wsEl.textContent.includes('CONNECTED');
    }, { timeout: 10000 });

    /* Let the oscilloscope accumulate steady-state phosphor traces */
    console.log('[HEADLESS] Warming up oscilloscope waveform history (3 seconds)...');
    await sleep(3000);

    // -------------------------------------------------------------------------
    // 1. Dashboard Nominal ASIL-D State
    // -------------------------------------------------------------------------
    console.log('[1/6] Capturing: 01_dashboard_nominal_asil_d.png...');
    const path01 = path.join(SCREENSHOTS_DIR, '01_dashboard_nominal_asil_d.png');
    await page.screenshot({ path: path01, fullPage: false });
    console.log(`      --> Saved: ${path01}`);

    // -------------------------------------------------------------------------
    // 2. CPU Saturation Storm Contention
    // -------------------------------------------------------------------------
    console.log('[2/6] Triggering CPU Core Saturation Storm (16 workers)...');
    await page.evaluate(() => {
        const range = document.getElementById('rangeStormThreads');
        if (range) {
            range.value = 16;
            range.dispatchEvent(new Event('input'));
        }
        const toggle = document.getElementById('toggleCpuStorm');
        if (toggle && !toggle.checked) toggle.click();
    });

    /* Wait 2.5 seconds for core saturation to establish and steady-state recovery to display */
    await sleep(2500);
    console.log('      Capturing: 02_cpu_saturation_storm.png...');
    const path02 = path.join(SCREENSHOTS_DIR, '02_cpu_saturation_storm.png');
    await page.screenshot({ path: path02, fullPage: false });
    console.log(`      --> Saved: ${path02}`);

    // -------------------------------------------------------------------------
    // 3. Priority Inversion & PIP Recovery
    // -------------------------------------------------------------------------
    console.log('[3/6] Injecting Microsecond Priority Inversion Trap...');
    await page.evaluate(() => {
        const trapBtn = document.getElementById('btnTriggerInversion');
        if (trapBtn) trapBtn.click();
    });

    /* Allow the 6.8 us spike, breach alert, PIP auto-healing, and terminal logs to display */
    await sleep(1500);
    console.log('      Capturing: 03_priority_inversion_pip_recovery.png...');
    const path03 = path.join(SCREENSHOTS_DIR, '03_priority_inversion_pip_recovery.png');
    await page.screenshot({ path: path03, fullPage: false });
    console.log(`      --> Saved: ${path03}`);

    /* Reset all faults before codelab demonstration */
    await page.evaluate(() => {
        const panicBtn = document.getElementById('btnPanicReset');
        if (panicBtn) panicBtn.click();
    });
    await sleep(1000);

    // -------------------------------------------------------------------------
    // 4. Interactive QNX 8.0 Codelab Modal Drawer
    // -------------------------------------------------------------------------
    console.log('[4/6] Opening Interactive QNX 8.0 Developer Codelab...');
    await page.evaluate(() => {
        const codelabBtn = document.getElementById('btnOpenCodelab');
        if (codelabBtn) codelabBtn.click();
    });
    await sleep(1200);

    /* Switch to Module 1 / Module 3 to showcase code and ASCII diagrams */
    await page.evaluate(() => {
        const tab1 = document.querySelector('.codelab-tab-btn[data-tab="module1"]');
        if (tab1) tab1.click();
    });
    await sleep(500);

    console.log('      Capturing: 04_interactive_codelab_modal.png...');
    const path04 = path.join(SCREENSHOTS_DIR, '04_interactive_codelab_modal.png');
    await page.screenshot({ path: path04, fullPage: false });
    console.log(`      --> Saved: ${path04}`);

    /* Close Codelab Drawer */
    await page.evaluate(() => {
        const closeCodelab = document.getElementById('btnCloseCodelab');
        if (closeCodelab) closeCodelab.click();
    });
    await sleep(800);

    // -------------------------------------------------------------------------
    // 5. ISO 26262 ASIL-D Audit Report Exporter Modal
    // -------------------------------------------------------------------------
    console.log('[5/6] Opening ISO 26262 ASIL-D Audit Report Dossier...');
    await page.evaluate(() => {
        const auditBtn = document.getElementById('btnExportAudit');
        if (auditBtn) auditBtn.click();
    });
    await sleep(1200);

    console.log('      Capturing: 05_asild_audit_report_exporter.png...');
    const path05 = path.join(SCREENSHOTS_DIR, '05_asild_audit_report_exporter.png');
    await page.screenshot({ path: path05, fullPage: false });
    console.log(`      --> Saved: ${path05}`);

    /* Close Audit Modal */
    await page.evaluate(() => {
        const closeAudit = document.getElementById('btnCloseAudit');
        if (closeAudit) closeAudit.click();
    });
    await sleep(800);

    // -------------------------------------------------------------------------
    // 6. Close-Up Crop of Hardware Oscilloscope Viewport
    // -------------------------------------------------------------------------
    console.log('[6/6] Capturing Close-Up Focus: 06_hardware_oscilloscope_focus.png...');
    const scopePanel = await page.$('.oscilloscope-panel');
    const path06 = path.join(SCREENSHOTS_DIR, '06_hardware_oscilloscope_focus.png');
    if (scopePanel) {
        await scopePanel.screenshot({ path: path06 });
        console.log(`      --> Saved: ${path06}`);
    } else {
        await page.screenshot({ path: path06, fullPage: false });
        console.log(`      --> Saved (fallback): ${path06}`);
    }

    await browser.close();

    console.log('===================================================================');
    console.log('  ALL 6 ULTRA-HD SCREENSHOTS CAPTURED SUCCESSFULLY!');
    console.log('===================================================================');

    /* List created files with exact byte sizes */
    const files = fs.readdirSync(SCREENSHOTS_DIR).filter(f => f.endsWith('.png'));
    files.forEach(file => {
        const fp = path.join(SCREENSHOTS_DIR, file);
        const stats = fs.statSync(fp);
        const sizeMb = (stats.size / (1024 * 1024)).toFixed(2);
        console.log(`  - ${file} (${sizeMb} MB)`);
    });
}

main().catch(err => {
    console.error('[FATAL ERROR IN SCREENSHOT PIPELINE]', err);
    process.exit(1);
});
