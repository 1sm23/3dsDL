// Progressive enhancements for the 3dsDL website. The page works without
// this file except for the "send a link" QR tool. Motion follows the
// conventions of ankush.one/mixtunes: short eased transitions, reveals on
// scroll, and nothing hidden until the script has confirmed it is running.
(function () {
  "use strict";

  var root = document.documentElement;
  root.classList.add("js");

  var REPO = "ankushKun/3dsDL";
  var PAGE_URL = location.origin + location.pathname;
  var reduceMotion = window.matchMedia("(prefers-reduced-motion: reduce)").matches;

  function formatBytes(bytes) {
    if (bytes < 1024) return Math.round(bytes) + " B";
    var units = ["KiB", "MiB", "GiB"];
    var value = bytes / 1024;
    var unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
      value /= 1024;
      unit += 1;
    }
    return value.toFixed(value < 10 ? 2 : 1) + " " + units[unit];
  }

  // Latest release: version, date and file sizes.
  function showRelease() {
    fetch("https://api.github.com/repos/" + REPO + "/releases/latest", {
      headers: { Accept: "application/vnd.github+json" }
    })
      .then(function (response) {
        if (!response.ok) throw new Error("HTTP " + response.status);
        return response.json();
      })
      .then(function (release) {
        var date = new Date(release.published_at);
        var when = isNaN(date)
          ? ""
          : ", " + new Intl.DateTimeFormat(undefined, { dateStyle: "medium" }).format(date);
        document.querySelectorAll("[data-release]").forEach(function (element) {
          element.textContent = "latest release " + release.tag_name + when;
        });
        (release.assets || []).forEach(function (asset) {
          document
            .querySelectorAll('[data-asset-size="' + asset.name + '"]')
            .forEach(function (element) {
              element.textContent = formatBytes(asset.size);
            });
        });
      })
      .catch(function () {
        // Leave the static text in place when GitHub can't be reached.
      });
  }

  // Downloads of every file in every release, pre-releases included: the
  // same sum as the shields.io badge in the README. The total is kept for an
  // hour, so a returning visitor sees it at once and the page spends as few
  // of GitHub's 60 requests an hour as it can.
  function showDownloads() {
    var targets = document.querySelectorAll("[data-downloads]");
    if (!targets.length || !window.fetch) return;
    var key = "downloads:" + REPO;
    var show = function (total) {
      targets.forEach(function (element) {
        element.textContent = total.toLocaleString("en") + (total === 1 ? " download" : " downloads");
        element.hidden = false;
      });
    };
    var saved = null;
    try {
      saved = JSON.parse(localStorage.getItem(key));
    } catch (error) {}
    if (saved && typeof saved.total === "number") {
      show(saved.total);
      if (Date.now() - saved.at < 60 * 60 * 1000) return;
    }
    var total = 0;
    var page = function (number) {
      return fetch("https://api.github.com/repos/" + REPO + "/releases?per_page=100&page=" + number, {
        headers: { Accept: "application/vnd.github+json" }
      })
        .then(function (response) {
          if (!response.ok) throw new Error("HTTP " + response.status);
          return response.json();
        })
        .then(function (releases) {
          releases.forEach(function (release) {
            (release.assets || []).forEach(function (asset) {
              total += asset.download_count || 0;
            });
          });
          if (releases.length === 100) return page(number + 1);
        });
    };
    page(1)
      .then(function () {
        show(total);
        try {
          localStorage.setItem(key, JSON.stringify({ total: total, at: Date.now() }));
        } catch (error) {}
      })
      .catch(function () {
        // Keep the saved count, or nothing, when GitHub can't be reached.
      });
  }

  function copyText(text, button) {
    var label = button.querySelector("span");
    var original = label ? label.textContent : "";
    var done = function (message) {
      if (!label) return;
      label.textContent = message;
      setTimeout(function () {
        label.textContent = original;
      }, 1600);
    };
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(text).then(
        function () { done("Copied"); },
        function () { done("Copy failed"); }
      );
    } else {
      done("Copy failed");
    }
  }

  function setUpCopyButtons() {
    document.querySelectorAll("[data-copy]").forEach(function (button) {
      button.addEventListener("click", function () {
        copyText(button.getAttribute("data-copy"), button);
      });
    });
  }

  // Same rule 3dsDL applies: http(s), a host, and no spaces or control
  // characters.
  function checkLink(value) {
    if (!value) return { ok: false, message: "The code appears as you type." };
    if (!/^https?:\/\//i.test(value)) {
      return { ok: false, message: "3dsDL only downloads links that start with http:// or https://." };
    }
    if (/[\s\u0000-\u001f\u007f]/.test(value)) {
      return { ok: false, message: "Links can't contain spaces. Copy the link again from its source." };
    }
    if (!/^https?:\/\/[^\/?#@\s]*@?[^\/?#\s]+/i.test(value)) {
      return { ok: false, message: "That link is missing a host name." };
    }
    if (/^http:\/\//i.test(value)) {
      return { ok: true, message: "Ready to scan. This link isn't encrypted, so 3dsDL will warn before downloading." };
    }
    return { ok: true, message: "Ready to scan. Open 3dsDL and choose Scan QR Code." };
  }

  function qrSvg(text, label) {
    var code = qrcode(0, "M");
    code.addData(text, "Byte");
    code.make();
    var count = code.getModuleCount();
    var border = 4;
    var size = count + border * 2;
    var path = "";
    for (var row = 0; row < count; row += 1) {
      for (var column = 0; column < count; column += 1) {
        if (code.isDark(row, column)) {
          path += "M" + (column + border) + " " + (row + border) + "h1v1h-1z";
        }
      }
    }
    var svg = document.createElementNS("http://www.w3.org/2000/svg", "svg");
    svg.setAttribute("viewBox", "0 0 " + size + " " + size);
    svg.setAttribute("shape-rendering", "crispEdges");
    svg.setAttribute("role", "img");
    svg.setAttribute("aria-label", label);
    var background = document.createElementNS(svg.namespaceURI, "rect");
    background.setAttribute("width", "100%");
    background.setAttribute("height", "100%");
    background.setAttribute("fill", "#fff");
    var modules = document.createElementNS(svg.namespaceURI, "path");
    modules.setAttribute("d", path);
    modules.setAttribute("fill", "#000");
    svg.appendChild(background);
    svg.appendChild(modules);
    return svg;
  }

  function setUpSendTool() {
    var input = document.getElementById("send-url");
    var status = document.getElementById("send-status");
    var preview = document.getElementById("send-qr");
    var bigButton = document.getElementById("send-big");
    var copyButton = document.getElementById("send-copy");
    var dialog = document.getElementById("qr-dialog");
    var dialogCode = document.getElementById("qr-dialog-code");
    var dialogUrl = document.getElementById("qr-dialog-url");
    if (!input || typeof qrcode !== "function") return;
    var placeholder = preview.innerHTML;
    var current = "";
    var timer = null;

    // Encode non-ASCII characters as UTF-8, like the 3DS keyboard does.
    if (qrcode.stringToBytesFuncs && qrcode.stringToBytesFuncs["UTF-8"]) {
      qrcode.stringToBytes = qrcode.stringToBytesFuncs["UTF-8"];
    }

    function render() {
      var value = input.value.trim();
      var result = checkLink(value);
      status.textContent = result.message;
      status.className = "status" + (value ? (result.ok ? " ok" : " error") : "");
      current = result.ok ? value : "";
      bigButton.disabled = !result.ok;
      copyButton.disabled = !result.ok;
      preview.innerHTML = "";
      if (!result.ok) {
        preview.innerHTML = placeholder;
        return;
      }
      try {
        preview.appendChild(qrSvg(value, "QR code for " + value));
      } catch (error) {
        preview.innerHTML = placeholder;
        status.textContent = "That link is too long for a QR code.";
        status.className = "status error";
        current = "";
        bigButton.disabled = true;
        copyButton.disabled = true;
      }
    }

    input.addEventListener("input", function () {
      clearTimeout(timer);
      timer = setTimeout(render, 120);
    });
    document.getElementById("send-form").addEventListener("submit", function (event) {
      event.preventDefault();
      render();
    });

    bigButton.addEventListener("click", function () {
      if (!current) return;
      dialogCode.innerHTML = "";
      dialogCode.appendChild(qrSvg(current, "Large QR code for " + current));
      dialogUrl.textContent = current;
      if (typeof dialog.showModal === "function") dialog.showModal();
    });
    dialog.addEventListener("click", function (event) {
      if (event.target === dialog) dialog.close();
    });

    // A link to this page that opens with the same QR code.
    copyButton.addEventListener("click", function () {
      if (!current) return;
      copyText(PAGE_URL + "?url=" + encodeURIComponent(current) + "#send", copyButton);
    });

    var shared = new URLSearchParams(location.search).get("url");
    if (shared) {
      input.value = shared;
      render();
    }
  }

  // The download on the 3DS's screens plays on a loop, using the same
  // wording as 3dsDL's progress screen.
  function setUpDeviceDemo() {
    var bar = document.querySelector("[data-demo-bar]");
    var amount = document.querySelector("[data-demo-amount]");
    var speed = document.querySelector("[data-demo-speed]");
    var state = document.querySelector("[data-demo-state]");
    var phase = document.querySelector("[data-demo-phase]");
    if (!bar || !amount || !speed || !state || !phase || reduceMotion) return;

    var total = 4.42 * 1048576;
    var received = 0.3 * total;
    var pauseUntil = 0;
    var step = 250;
    var smoothed = 0.65 * 1048576;

    function show(fraction) {
      bar.style.setProperty("--p", fraction.toFixed(4));
      amount.textContent =
        formatBytes(received) + " of " + formatBytes(total) + " (" + Math.floor(fraction * 100) + "%)";
    }

    setInterval(function () {
      if (document.hidden) return;
      var now = Date.now();
      if (now < pauseUntil) return;
      if (received >= total) {
        received = 0;
        phase.textContent = "connecting";
        speed.textContent = "";
        show(0);
        pauseUntil = now + 900;
        return;
      }
      var rate = (0.5 + Math.random() * 0.3) * 1048576;
      smoothed += 0.3 * (rate - smoothed);
      received = Math.min(total, received + rate * (step / 1000));
      show(received / total);
      if (received >= total) {
        state.textContent = "done";
        phase.textContent = "complete";
        speed.textContent = "Saved as sdmc:/cias/Universal-Updater.cia";
        pauseUntil = now + 2800;
      } else {
        state.textContent = "downloading";
        phase.textContent = "downloading";
        var left = Math.max(1, Math.round((total - received) / smoothed));
        speed.textContent = formatBytes(smoothed) + "/s, " + left + "s left";
      }
    }, step);
  }

  // The screens are drawn at native 3DS size and projected onto the photo by
  // fixed matrices computed for a 1400 px wide image, so they need scaling
  // by (rendered width / 1400). CSS cannot turn a length into a number, so
  // the factor is set here and kept current as the stage resizes.
  function setUpScreenScale() {
    document.querySelectorAll(".device-stage").forEach(function (stage) {
      var photoWidth = parseFloat(getComputedStyle(stage).getPropertyValue("--photo-w"));
      if (!photoWidth) return;
      function update() {
        var width = stage.offsetWidth;
        if (width > 0) stage.style.setProperty("--k", (width / photoWidth).toFixed(5));
      }
      update();
      if ("ResizeObserver" in window) {
        new ResizeObserver(update).observe(stage);
      } else {
        window.addEventListener("resize", update);
      }
      stage.classList.add("is-mapped");
    });
  }

  // A slight tilt of the device towards the pointer. Pointer only (no touch),
  // and off when reduced motion is requested.
  function setUpTilt() {
    var device = document.querySelector("[data-tilt]");
    var stage = device && device.querySelector(".device-stage");
    if (!device || !stage || reduceMotion) return;
    if (!window.matchMedia("(hover: hover) and (pointer: fine)").matches) return;

    var frame = null;
    function apply(x, y) {
      stage.style.transform = "rotateX(" + (y * -4).toFixed(2) + "deg) rotateY(" + (x * 6).toFixed(2) + "deg)";
    }
    device.addEventListener("pointermove", function (event) {
      var box = device.getBoundingClientRect();
      var x = (event.clientX - box.left) / box.width - 0.5;
      var y = (event.clientY - box.top) / box.height - 0.5;
      if (frame) cancelAnimationFrame(frame);
      frame = requestAnimationFrame(function () { apply(x, y); });
    });
    device.addEventListener("pointerleave", function () {
      if (frame) cancelAnimationFrame(frame);
      stage.style.transform = "";
    });
  }

  // FAQ rows open and close with an eased height animation and a short fade
  // of the answer. Without the script, or with reduced motion, <details>
  // simply opens and closes on its own.
  function setUpFaq() {
    if (reduceMotion || !("animate" in Element.prototype)) return;
    var timing = { duration: 320, easing: "cubic-bezier(0.23, 1, 0.32, 1)" };

    document.querySelectorAll(".faq details").forEach(function (details) {
      var summary = details.querySelector("summary");
      var answer = summary && summary.nextElementSibling;
      if (!answer) return;
      var running = null;

      function settle(open) {
        details.open = open;
        details.classList.remove("is-closing");
        details.style.overflow = "";
        running = null;
      }

      function animate(from, to, opening) {
        if (running) running.cancel();
        details.style.overflow = "hidden";
        running = details.animate({ height: [from + "px", to + "px"] }, timing);
        answer.animate(
          opening
            ? { opacity: [0, 1], transform: ["translateY(-6px)", "none"] }
            : { opacity: [1, 0], transform: ["none", "translateY(-4px)"] },
          { duration: opening ? 280 : 200, easing: timing.easing, fill: "none" }
        );
        var current = running;
        // finished rejects when a later click cancels this animation; the
        // newer animation then owns the cleanup.
        running.finished.then(function () {
          if (running === current) settle(opening);
        }, function () {});
      }

      summary.addEventListener("click", function (event) {
        event.preventDefault();
        // Start from the height on screen, even mid-animation. Fractional
        // sizes avoid a 1px snap when the animation hands over.
        var height = function (element) { return element.getBoundingClientRect().height; };
        var style = getComputedStyle(details);
        var frame = parseFloat(style.borderTopWidth) + parseFloat(style.borderBottomWidth);
        var from = height(details);
        if (details.open && !details.classList.contains("is-closing")) {
          details.classList.add("is-closing");
          animate(from, height(summary) + frame, false);
        } else {
          details.classList.remove("is-closing");
          details.open = true;
          animate(from, height(summary) + height(answer) + frame, true);
        }
      });
    });
  }

  // The install tabs. Arrow keys, Home and End move between them (a roving
  // tabindex, as in the ARIA tabs pattern). Switching slides the old QR code
  // and details off both screens and the new ones on, in the tab's direction.
  function setUpInstaller() {
    var picker = document.querySelector(".picker");
    var panel = document.getElementById("install-screens");
    if (!picker || !panel) return;
    var picks = Array.prototype.slice.call(picker.querySelectorAll(".pick"));
    var animated = !reduceMotion && "animate" in Element.prototype;
    var easeOut = "cubic-bezier(0.23, 1, 0.32, 1)";
    var easeIn = "cubic-bezier(0.4, 0, 1, 1)";
    var views = Array.prototype.slice.call(panel.querySelectorAll(".view"));
    var turn = 0;

    function stopAll() {
      panel.querySelectorAll(".view, .qr-tile").forEach(function (element) {
        element.getAnimations().forEach(function (animation) { animation.cancel(); });
      });
    }

    function showOnly(method) {
      views.forEach(function (view) { view.hidden = view.dataset.view !== method; });
    }

    function swap(method, direction) {
      var mine = ++turn;
      stopAll();
      var leaving = views.filter(function (view) {
        return !view.hidden && view.dataset.view !== method;
      });
      if (!animated || !leaving.length) {
        showOnly(method);
        return;
      }

      var exits = leaving.map(function (view, index) {
        return view.animate(
          { opacity: [1, 0], transform: ["none", "translateX(" + -direction * 24 + "px)"] },
          { duration: 160, delay: index * 30, easing: easeIn, fill: "forwards" }
        ).finished;
      });
      // A rejection means a newer click cancelled this swap and owns the views.
      Promise.all(exits).then(function () {
        if (mine !== turn) return;
        showOnly(method);
        stopAll();
        views.filter(function (view) { return !view.hidden; }).forEach(function (view, index) {
          var timing = { duration: 320, delay: index * 50, easing: easeOut, fill: "backwards" };
          view.animate(
            { opacity: [0, 1], transform: ["translateX(" + direction * 24 + "px)", "none"] },
            timing
          );
          var tile = view.querySelector(".qr-tile");
          if (tile) {
            tile.animate({ transform: ["scale(0.92)", "none"] }, { duration: 380, easing: easeOut, fill: "backwards" });
          }
        });
      }, function () {});
    }

    function select(pick, focus) {
      if (focus) pick.focus();
      if (pick.getAttribute("aria-selected") === "true") return;
      var before = picks.findIndex(function (other) {
        return other.getAttribute("aria-selected") === "true";
      });
      picks.forEach(function (other) {
        var on = other === pick;
        other.setAttribute("aria-selected", on ? "true" : "false");
        other.tabIndex = on ? 0 : -1;
      });
      picker.dataset.active = pick.dataset.method;
      panel.setAttribute("aria-labelledby", pick.id);
      swap(pick.dataset.method, picks.indexOf(pick) > before ? 1 : -1);
    }

    picks.forEach(function (pick) {
      pick.addEventListener("click", function () { select(pick, false); });
    });

    picker.addEventListener("keydown", function (event) {
      var index = picks.indexOf(document.activeElement);
      if (index < 0) return;
      var next = {
        ArrowLeft: (index - 1 + picks.length) % picks.length,
        ArrowRight: (index + 1) % picks.length,
        Home: 0,
        End: picks.length - 1
      }[event.key];
      if (next === undefined) return;
      event.preventDefault();
      select(picks[next], true);
    });
  }

  // Scroll reveals. The reveal-ready class is added only here, so sections
  // are never hidden unless this code is running and can show them again.
  function setUpReveals() {
    var items = document.querySelectorAll(".reveal");
    if (!items.length || reduceMotion || !("IntersectionObserver" in window)) return;
    root.classList.add("reveal-ready");
    var observer = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        if (entry.isIntersecting) {
          entry.target.classList.add("is-in");
          observer.unobserve(entry.target);
        }
      });
    }, { rootMargin: "0px 0px -8% 0px", threshold: 0.08 });
    items.forEach(function (item) { observer.observe(item); });
  }

  // Highlight the tab for the section in view.
  function setUpTabs() {
    var tabs = Array.prototype.slice.call(document.querySelectorAll(".tabs a"));
    if (!tabs.length || !("IntersectionObserver" in window)) return;
    var byId = {};
    tabs.forEach(function (tab) { byId[tab.getAttribute("href").slice(1)] = tab; });
    var observer = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        var tab = byId[entry.target.id];
        if (!tab) return;
        if (entry.isIntersecting) {
          tabs.forEach(function (other) { other.classList.toggle("is-active", other === tab); });
        }
      });
    }, { rootMargin: "-40% 0px -50% 0px" });
    Object.keys(byId).forEach(function (id) {
      var section = document.getElementById(id);
      if (section) observer.observe(section);
    });
  }

  // Menu sounds, synthesised on the fly. On by default; browsers only allow
  // audio after the first click or key press, so they start from then.
  function setUpSounds() {
    var toggle = document.getElementById("sound-toggle");
    var AudioContextClass = window.AudioContext || window.webkitAudioContext;
    if (!toggle || !AudioContextClass) {
      if (toggle) toggle.hidden = true;
      return;
    }
    var context = null;
    var enabled = true;
    var lastHover = 0;

    try {
      enabled = localStorage.getItem("3dsdl-sounds") !== "off";
    } catch (error) {
      enabled = true;
    }

    function unlock() {
      if (!context) context = new AudioContextClass();
      if (context.state === "suspended") context.resume();
    }
    window.addEventListener("pointerdown", unlock, { passive: true });
    window.addEventListener("keydown", unlock);

    function blip(frequency, endFrequency, duration, volume) {
      if (!enabled || !context || context.state !== "running") return;
      var now = context.currentTime;
      var oscillator = context.createOscillator();
      var gain = context.createGain();
      oscillator.type = "sine";
      oscillator.frequency.setValueAtTime(frequency, now);
      oscillator.frequency.exponentialRampToValueAtTime(endFrequency, now + duration);
      gain.gain.setValueAtTime(volume, now);
      gain.gain.exponentialRampToValueAtTime(0.0001, now + duration);
      oscillator.connect(gain).connect(context.destination);
      oscillator.start(now);
      oscillator.stop(now + duration + 0.02);
    }

    function render() {
      toggle.setAttribute("aria-pressed", enabled ? "true" : "false");
      toggle.querySelector("use").setAttribute("href", enabled ? "#i-sound" : "#i-mute");
      toggle.querySelector("span").textContent = enabled ? "Menu sounds on" : "Menu sounds off";
    }
    render();

    toggle.addEventListener("click", function () {
      enabled = !enabled;
      try {
        localStorage.setItem("3dsdl-sounds", enabled ? "on" : "off");
      } catch (error) {
        // Remembering the choice is a nicety; it still applies to this visit.
      }
      render();
      blip(660, 990, 0.08, 0.06);
    });

    document.addEventListener("pointerover", function (event) {
      var target = event.target.closest("[data-sound], .btn, .tabs a");
      if (!target || target.contains(event.relatedTarget)) return;
      var now = Date.now();
      if (now - lastHover < 70) return;
      lastHover = now;
      blip(1800, 1500, 0.03, 0.03);
    });
    document.addEventListener("click", function (event) {
      if (event.target.closest("[data-sound], .btn, summary, .tabs a")) {
        blip(520, 880, 0.09, 0.06);
      }
    });
  }

  showRelease();
  showDownloads();
  setUpCopyButtons();
  setUpSendTool();
  setUpScreenScale();
  setUpDeviceDemo();
  setUpTilt();
  setUpFaq();
  setUpInstaller();
  setUpReveals();
  setUpTabs();
  setUpSounds();
})();
