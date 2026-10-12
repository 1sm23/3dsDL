// Progressive enhancements for the 3dsDL website. Everything on the page
// works without this file except the "send a link" QR tool.
(function () {
  "use strict";

  var REPO = "ankushKun/3dsDL";
  var PAGE_URL = location.origin + location.pathname;

  function formatBytes(bytes) {
    if (bytes < 1024) return bytes + " B";
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
          : " · " + new Intl.DateTimeFormat(undefined, { dateStyle: "medium" }).format(date);
        document.querySelectorAll("[data-release]").forEach(function (element) {
          element.textContent = "Latest release " + release.tag_name + when;
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
    var placeholder = preview.innerHTML;
    var current = "";
    var timer = null;

    if (typeof qrcode !== "function") return;
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

  showRelease();
  setUpCopyButtons();
  setUpSendTool();
})();
