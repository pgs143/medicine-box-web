/* ═══════════════════════════════════════════════════════════
   auth.js — Firebase Authentication (Login / Register)
   ═══════════════════════════════════════════════════════════ */

import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.0/firebase-app.js";
import {
  getAuth,
  signInWithEmailAndPassword,
  createUserWithEmailAndPassword,
  onAuthStateChanged
} from "https://www.gstatic.com/firebasejs/10.12.0/firebase-auth.js";

import { firebaseConfig } from "./firebase-config.js";

// ─── Init ────────────────────────────────────────────────
const app  = initializeApp(firebaseConfig);
const auth = getAuth(app);

// ─── Element refs ───────────────────────────────────────
const form       = document.getElementById("auth-form");
const elEmail    = document.getElementById("email");
const elPassword = document.getElementById("password");
const elMessage  = document.getElementById("message");
const btnSubmit  = document.getElementById("btn-submit");

let mode = "login";   // "login" | "register"

// ═══════════════════════════════════════════════════════════
//  สลับ Tab Login / Register
// ═══════════════════════════════════════════════════════════
document.querySelectorAll(".login-tab").forEach(tab => {
  tab.onclick = () => {
    document.querySelectorAll(".login-tab").forEach(t => t.classList.remove("active"));
    tab.classList.add("active");

    mode = tab.dataset.mode;
    btnSubmit.textContent = (mode === "login") ? "เข้าสู่ระบบ" : "สมัครสมาชิก";
    showMessage("", "");
  };
});

// ═══════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════
function showMessage(text, type) {
  elMessage.textContent = text;
  elMessage.className = "message" + (type ? " " + type : "");
}

function errorToThai(code) {
  const map = {
    "auth/invalid-email":          "อีเมลไม่ถูกต้อง",
    "auth/user-not-found":         "ไม่พบบัญชีนี้ในระบบ",
    "auth/wrong-password":         "รหัสผ่านไม่ถูกต้อง",
    "auth/invalid-credential":     "อีเมลหรือรหัสผ่านไม่ถูกต้อง",
    "auth/email-already-in-use":   "อีเมลนี้ถูกใช้ไปแล้ว",
    "auth/weak-password":          "รหัสผ่านต้องมีอย่างน้อย 6 ตัวอักษร",
    "auth/too-many-requests":      "พยายามหลายครั้งเกินไป ลองใหม่ภายหลัง",
    "auth/network-request-failed": "การเชื่อมต่อขัดข้อง ตรวจสอบอินเทอร์เน็ต",
    "auth/operation-not-allowed":  "ยังไม่เปิดใช้ Email/Password ใน Firebase Console",
    "auth/configuration-not-found": "ยังไม่ได้ตั้งค่า Firebase Authentication: เปิด Authentication > Sign-in method > Email/Password ใน Firebase Console"
  };
  return map[code] || "เกิดข้อผิดพลาด: " + code;
}

// ═══════════════════════════════════════════════════════════
//  Submit Form
// ═══════════════════════════════════════════════════════════
form.onsubmit = async (e) => {
  e.preventDefault();

  const email    = elEmail.value.trim();
  const password = elPassword.value;

  btnSubmit.disabled = true;
  showMessage("⏳ กำลังดำเนินการ...", "");

  try {
    if (mode === "login") {
      await signInWithEmailAndPassword(auth, email, password);
      showMessage("✅ เข้าสู่ระบบสำเร็จ กำลังไปหน้าหลัก...", "success");
    } else {
      await createUserWithEmailAndPassword(auth, email, password);
      showMessage("✅ สมัครสมาชิกสำเร็จ กำลังไปหน้าหลัก...", "success");
    }
  } catch (err) {
    console.error(err);
    showMessage("❌ " + errorToThai(err.code), "error");
    btnSubmit.disabled = false;
  }
};

// ═══════════════════════════════════════════════════════════
//  ถ้า login แล้ว → ไป index.html ทันที
// ═══════════════════════════════════════════════════════════
onAuthStateChanged(auth, (user) => {
  if (user) {
    setTimeout(() => {
      window.location.href = "index.html";
    }, 600);
  } else {
    btnSubmit.disabled = false;
  }
});