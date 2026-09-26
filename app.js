/* ═══════════════════════════════════════════════════════════
   app.js — Logic หลักของกล่องยา 6 ช่อง
   ✅ แสดงยา 6 ช่อง
   ✅ แก้ไขผ่าน modal
   ✅ ดูประวัติการกินยา (events)
   ✅ กรองตามวันที่ + สถานะ
   ✅ อัปเดต real-time
   ═══════════════════════════════════════════════════════════ */

import { getApp, getApps, initializeApp } from "https://www.gstatic.com/firebasejs/10.12.0/firebase-app.js";
import {
  getDatabase, ref, onValue, set
} from "https://www.gstatic.com/firebasejs/10.12.0/firebase-database.js";

import { firebaseConfig } from "./firebase-config.js";

// ─── Init ────────────────────────────────────────────────
const app = getApps().length ? getApp() : initializeApp(firebaseConfig);
let db = null;
try {
  db = getDatabase(app);
} catch (error) {
  console.error(error);
}

// ─── Cache ───────────────────────────────────────────────
let slotsCache  = {};
let eventsCache = {};
let structuredSlotsCache = {};

// ─── Element refs ────────────────────────────────────────
const elStatus      = document.getElementById("status");
const elSlots       = document.getElementById("slots");
const elHistoryBody = document.getElementById("history-body");
const elStats       = document.getElementById("stats");
const modal         = document.getElementById("modal");
const filterDate    = document.getElementById("filter-date");
const filterStatus  = document.getElementById("filter-status");
const timesList     = document.getElementById("f-times-list");
let editingSlot = null;

document.querySelectorAll(".tab").forEach((tab) => {
  tab.addEventListener("click", () => {
    document.querySelectorAll(".tab").forEach((item) => item.classList.remove("active"));
    document.querySelectorAll(".tab-content").forEach((content) => content.classList.remove("active"));
    tab.classList.add("active");
    document.getElementById(`tab-${tab.dataset.tab}`)?.classList.add("active");
  });
});

const defaultSlot = (slot) => ({ slot, name: "ยังไม่ได้ตั้งค่า", mg: "", schedules: [], enabled: false });

function normalizeSlot(raw, slot) {
  const value = raw || {};
  const normalizeTime = (time) => {
    const match = String(time).trim().match(/^(\d{1,2}):(\d{2})$/);
    return match ? `${match[1].padStart(2, "0")}:${match[2]}` : String(time).trim();
  };
  const oldTimes = (Array.isArray(value.times) ? value.times : String(value.times || "").split(",")).map(normalizeTime).filter(Boolean);
  const schedules = Array.isArray(value.schedules)
    ? value.schedules.map((item) => ({ time: normalizeTime(item.time), meal: item.meal_relation ?? item.meal ?? "after_meal" })).filter((item) => item.time)
    : oldTimes.map((time) => ({ time, meal: value.meal_relation ?? value.meal ?? "after_meal" }));
  return {
    ...defaultSlot(slot),
    name: value.medicine_name ?? value.name ?? "ยังไม่ได้ตั้งค่า",
    mg: value.dosage_mg ?? value.mg ?? "",
    schedules,
    enabled: Boolean(value.enabled)
  };
}

function getSlot(slot) {
  const medicine = normalizeSlot(structuredSlotsCache[`slot${slot}`] || slotsCache[slot], slot);
  return { ...medicine, schedules: Array.isArray(medicine.schedules) ? medicine.schedules : [] };
}

function mealLabel(meal) {
  return { before_meal: "ก่อนอาหาร", after_meal: "หลังอาหาร", immediate: "กินทันที / ก่อนนอน" }[meal] || meal;
}

function parseEvent(key, rawEvent) {
  const event = rawEvent || {};
  const match = String(key).match(/^slot(\d+)_(\d{4}-\d{2}-\d{2})_(\d{2}:\d{2})$/);
  const slot = Number(event.slot || (match ? match[1] : 0));
  const medicine = getSlot(slot);
  const formatTimestamp = (value) => value ? new Date(Number(value) * 1000).toLocaleString("th-TH", { dateStyle: "short", timeStyle: "short" }) : "-";
  return {
    ...event,
    date: event.date || (match ? match[2] : "-"),
    time: event.time || (match ? match[3] : "-"),
    slot,
    name: event.name || medicine.name,
    mg: event.mg || medicine.mg,
    status: event.status || "pending",
    confirmedAt: event.confirmed_at ? formatTimestamp(event.confirmed_at) : (event.confirmedAt || "-")
  };
}

function statusLabel(status) {
  return { confirmed: "กินแล้ว", notified: "แจ้งแล้ว", missed: "พลาด", pending: "รอดำเนินการ" }[status] || status;
}

function timeOptions(selected = "") {
  const options = ['<option value="">เลือกเวลา</option>'];
  for (let hour = 0; hour < 24; hour += 1) {
    for (let minute = 0; minute < 60; minute += 1) {
      const time = `${String(hour).padStart(2, "0")}:${String(minute).padStart(2, "0")}`;
      options.push(`<option value="${time}" ${time === selected ? "selected" : ""}>${time}</option>`);
    }
  }
  return options.join("");
}

function mealOptions(selected = "after_meal") {
  return [["before_meal", "ก่อนอาหาร"], ["after_meal", "หลังอาหาร"], ["immediate", "ก่อนนอน / ทันที"]]
    .map(([value, label]) => `<option value="${value}" ${value === selected ? "selected" : ""}>${label}</option>`).join("");
}

function addTimeRow(value = "", meal = "after_meal") {
  if (!timesList) return;
  const row = document.createElement("div");
  row.className = "time-editor-row";
  row.innerHTML = `<select class="time-select" aria-label="เวลาที่ต้องกิน">${timeOptions(value)}</select><select class="meal-select" aria-label="มื้ออาหาร">${mealOptions(meal)}</select><button type="button" class="btn-remove-time" aria-label="ลบเวลานี้">ลบ</button>`;
  row.querySelector(".btn-remove-time").addEventListener("click", () => {
    row.remove();
    if (!timesList.children.length) addTimeRow();
  });
  timesList.appendChild(row);
}

function getSelectedSchedules() {
  return [...(timesList?.querySelectorAll(".time-editor-row") || [])].map((row) => ({
    time: row.querySelector(".time-select").value,
    meal_relation: row.querySelector(".meal-select").value
  })).filter((item) => item.time);
}

function setStatus(text, online = false) {
  if (!elStatus) return;
  elStatus.textContent = online ? "🟢 เชื่อมต่อแล้ว" : text;
  elStatus.className = `status ${online ? "online" : "offline"}`;
}

function escapeHtml(value) {
  return String(value).replace(/[&<>'"]/g, (character) => ({
    "&": "&amp;", "<": "&lt;", ">": "&gt;", "'": "&#39;", '"': "&quot;"
  }[character]));
}

function renderSlots() {
  if (!elSlots) return;
  elSlots.innerHTML = "";
  for (let slot = 1; slot <= 6; slot += 1) {
    const medicine = getSlot(slot);
    const card = document.createElement("button");
    card.className = `slot-card ${medicine.enabled ? "enabled" : "disabled"}`;
    card.type = "button";
    card.innerHTML = `<span class="slot-card-top"><span class="slot-number">ช่อง ${slot}</span><span class="slot-state">${medicine.enabled ? "ใช้งานอยู่" : "ปิดใช้งาน"}</span></span><strong class="medicine-name">${escapeHtml(medicine.name)}</strong><span class="dosage">${medicine.mg ? `${escapeHtml(medicine.mg)} mg` : "ยังไม่ระบุขนาดยา"}</span><span class="times-label">ตารางการกินยา</span><span class="times">${medicine.schedules.length ? medicine.schedules.map((item) => `<span class="time-chip"><b>${escapeHtml(item.time)}</b> ${escapeHtml(mealLabel(item.meal))}</span>`).join("") : "<span class=\"no-time\">ยังไม่ได้ตั้งเวลา</span>"}</span>`;
    card.addEventListener("click", () => openModal(slot, medicine));
    elSlots.appendChild(card);
  }
}

function renderHistory() {
  if (!elHistoryBody) return;
  const date = filterDate?.value || "";
  const status = filterStatus?.value || "";
  const events = Object.entries(eventsCache).map(([key, event]) => parseEvent(key, event)).filter((event) => (!date || event.date === date) && (!status || event.status === status)).sort((a, b) => `${b.date}${b.time}`.localeCompare(`${a.date}${a.time}`));
  elHistoryBody.innerHTML = events.length ? events.map((event) => `<tr><td>${escapeHtml(event.date)}</td><td>${escapeHtml(event.time)}</td><td>ช่อง ${escapeHtml(event.slot)}</td><td>${escapeHtml(event.name)}</td><td>${escapeHtml(event.mg || "-")} mg</td><td><span class="badge ${escapeHtml(event.status)}">${escapeHtml(statusLabel(event.status))}</span></td><td>${escapeHtml(event.confirmedAt)}</td></tr>`).join("") : '<tr><td colspan="7" class="empty">ยังไม่มีประวัติ</td></tr>';
  if (elStats) elStats.textContent = `ทั้งหมด ${events.length} รายการ`;
}

function openModal(slot, medicine) {
  editingSlot = slot;
  document.getElementById("modal-slot").textContent = slot;
  document.getElementById("f-name").value = medicine.name === "ยังไม่ได้ตั้งค่า" ? "" : medicine.name;
  document.getElementById("f-mg").value = medicine.mg || "";
  timesList.innerHTML = "";
  (medicine.schedules.length ? medicine.schedules : [{ time: "", meal: "after_meal" }]).forEach((item) => addTimeRow(item.time, item.meal));
  document.getElementById("f-enabled").checked = Boolean(medicine.enabled);
  modal?.classList.remove("hidden");
}

function closeModal() {
  editingSlot = null;
  modal?.classList.add("hidden");
}

document.getElementById("btn-cancel")?.addEventListener("click", closeModal);
modal?.addEventListener("click", (event) => { if (event.target === modal) closeModal(); });
document.getElementById("btn-refresh")?.addEventListener("click", () => {
  if (filterDate) filterDate.value = "";
  if (filterStatus) filterStatus.value = "";
  renderHistory();
});
filterDate?.addEventListener("change", renderHistory);
filterStatus?.addEventListener("change", renderHistory);
document.getElementById("btn-add-time")?.addEventListener("click", () => addTimeRow());

document.getElementById("btn-save")?.addEventListener("click", async () => {
  if (!editingSlot || !db) return;
  const medicine = {
    medicine_name: document.getElementById("f-name").value.trim() || "ยังไม่ได้ตั้งค่า",
    dosage_mg: Number(document.getElementById("f-mg").value) || 0,
    schedules: getSelectedSchedules(),
    meal_relation: getSelectedSchedules()[0]?.meal_relation || "after_meal",
    enabled: document.getElementById("f-enabled").checked
  };
  try {
    await set(ref(db, `medicine_box/slots/slot${editingSlot}`), medicine);
    closeModal();
  } catch (error) {
    console.error(error);
    setStatus("🔴 บันทึกไม่สำเร็จ");
    alert("บันทึกไม่สำเร็จ กรุณาตรวจสอบ Firebase Rules และการเชื่อมต่ออินเทอร์เน็ต");
  }
});

renderSlots();
renderHistory();

if (db) {
  onValue(ref(db, "medicine_box/slots"), (snapshot) => {
    structuredSlotsCache = snapshot.val() || {};
    renderSlots();
    renderHistory();
    setStatus("เชื่อมต่อแล้ว", true);
  }, (error) => {
    console.error(error);
  });

  onValue(ref(db, "events"), (snapshot) => {
    eventsCache = snapshot.val() || {};
    renderHistory();
  }, (error) => {
    console.error(error);
    setStatus("🔴 อ่านข้อมูล Firebase ไม่สำเร็จ");
  });

  onValue(ref(db, "medicine_box/events"), (snapshot) => {
    if (snapshot.exists()) {
      eventsCache = { ...eventsCache, ...(snapshot.val() || {}) };
      renderHistory();
    }
  });
} else {
  setStatus("🔴 ยังไม่ได้ตั้งค่า Firebase Database");
}