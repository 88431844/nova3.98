# README Weather Preview Screenshot Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a full-page screenshot of the current weather preview to the README, with explanatory text below it, and push the result to `nova3.98-weather`.

**Architecture:** Serve the existing dependency-free `weather-preview/` page locally, capture its complete desktop presentation without changing the page, and store the PNG under `docs/images/`. Reference that stable repository asset from the current weather feature section in `README.md`.

**Tech Stack:** Static HTML/CSS/JavaScript, in-app browser screenshot tooling, PNG, Markdown, Python regression tests, Git.

---

### Task 1: Capture the complete weather preview frame

**Files:**
- Create: `docs/images/weather-dashboard-preview.png`
- Verify: `weather-preview/index.html`

- [x] **Step 1: Start the existing local preview**

Run from the repository root:

```bash
python3 -m http.server 8000
```

Expected: the server accepts requests at `http://127.0.0.1:8000/weather-preview/`.

- [x] **Step 2: Open and validate the preview**

Open `http://127.0.0.1:8000/weather-preview/` in a 1200x900 desktop viewport. Wait until `#data-status` reports either live weather or the explicit sample-data fallback. Confirm the complete `.preview-shell`, including the frame and status line, is visible without overlap or horizontal scrolling.

- [x] **Step 3: Capture the complete page presentation**

Save the screenshot as:

```text
docs/images/weather-dashboard-preview.png
```

Expected: a non-empty PNG containing the page background, full panel frame, weather content, and status text.

- [x] **Step 4: Verify the PNG**

Run:

```bash
file docs/images/weather-dashboard-preview.png
```

Expected: `PNG image data` with non-zero desktop dimensions. Visually inspect the saved file and confirm it is not blank, clipped, or overlapped.

### Task 2: Embed the screenshot in README

**Files:**
- Modify: `README.md:25`

- [x] **Step 1: Add the image and caption**

Insert this Markdown after the current weather feature list and before the icon source paragraph:

```markdown
![深圳天气墨水屏网页预览](docs/images/weather-dashboard-preview.png)

上图为完整网页预览框架；屏幕区域与设备共享 768x552 坐标，实时天气接口不可用时会显示示例数据。
```

- [x] **Step 2: Verify README references**

Run a local-path check that parses Markdown image links and asserts that `docs/images/weather-dashboard-preview.png` exists.

Expected: no missing README image paths.

### Task 3: Test, commit, and push

**Files:**
- Verify: `README.md`
- Verify: `docs/images/weather-dashboard-preview.png`
- Verify: `docs/superpowers/plans/2026-08-22-readme-weather-preview-screenshot.md`

- [x] **Step 1: Run regression checks**

Run:

```bash
python3 tools/test_weather_preview.py
git diff --check
```

Expected: all preview tests pass and Git reports no whitespace errors.

- [x] **Step 2: Confirm the exact commit scope**

Stage only:

```bash
git add README.md docs/images/weather-dashboard-preview.png docs/superpowers/plans/2026-08-22-readme-weather-preview-screenshot.md
```

Expected: existing modifications to `SE0398NZ07A0_NodeMCU_Test/SE0398NZ07A0_NodeMCU_Test.ino` and unrelated untracked files remain unstaged.

- [ ] **Step 3: Commit the implementation**

```bash
git commit -m "docs: add weather dashboard preview image"
```

- [ ] **Step 4: Push and verify the remote hash**

Push `nova3.98-weather` with the verified deploy key, then compare `git rev-parse HEAD` with `git ls-remote origin refs/heads/nova3.98-weather`.

Expected: the local and remote commit hashes are identical.
