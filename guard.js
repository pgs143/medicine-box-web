/* ═══════════════════════════════════════════════
   Guard — ตรวจสอบว่าผู้ใช้ login อยู่หรือไม่
   ถ้าไม่ login → เด้งไป login.html
   ═══════════════════════════════════════════════ */

import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.0/firebase-app.js";
import {
  getAuth, onAuthStateChanged, signOut
} from "https://www.gstatic.com/firebasejs/10.12.0/firebase-auth.js";

import { firebaseConfig } from "./firebase-config.js";

const app  = initializeApp(firebaseConfig);
const auth = getAuth(app);

// ─── ตรวจสอบสถานะ login ────────────────────────
onAuthStateChanged(auth, (user) => {
  if (!user) {
    // ยังไม่ login → เด้งไปหน้า login
    window.location.href = "login.html";
  } else {
    // login แล้ว → แสดงอีเมลที่ header
    const el = document.getElementById("user-email");
    if (el) el.textContent = user.email;
  }
});

// ─── ปุ่ม logout ────────────────────────────────
const btnLogout = document.getElementById("btn-logout");
if (btnLogout) {
  btnLogout.onclick = async () => {
    if (confirm("ต้องการออกจากระบบใช่ไหม?")) {
      await signOut(auth);
      window.location.href = "login.html";
    }
  };
}