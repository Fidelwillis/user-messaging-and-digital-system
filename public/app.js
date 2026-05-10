let currentUser = "";

async function post(path, data) {
  const body = new URLSearchParams(data);
  const res = await fetch(path, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body
  });
  const text = await res.text();
  let json = {};
  try { json = JSON.parse(text); } catch {}
  return { ok: res.ok, json, text };
}

function status(msg) {
  document.getElementById("status").textContent = msg;
}

async function registerUser() {
  const username = document.getElementById("username").value.trim();
  const password = document.getElementById("password").value;
  const result = await post("/api/register", { username, password });
  status(result.json.message || result.text);
}

async function loginUser() {
  const username = document.getElementById("username").value.trim();
  const password = document.getElementById("password").value;
  const result = await post("/api/login", { username, password });
  if (result.ok) currentUser = username;
  status(result.json.message || result.text);
}

async function logoutUser() {
  const username = currentUser || document.getElementById("username").value.trim();
  const result = await post("/api/logout", { username });
  if (result.ok) currentUser = "";
  status(result.json.message || result.text);
}

async function sendMessage() {
  if (!currentUser) return status("Login first");
  const receiver = document.getElementById("receiver").value.trim();
  const content = document.getElementById("content").value.trim();
  const result = await post("/api/send", { sender: currentUser, receiver, content });
  status(result.json.message || result.text);
}

async function loadHistory() {
  if (!currentUser) return status("Login first");
  const user2 = document.getElementById("historyUser").value.trim();
  const result = await post("/api/history", { user1: currentUser, user2 });
  if (!result.ok) return status("Could not load history");

  const list = document.getElementById("history");
  list.innerHTML = "";
  result.json.forEach(m => {
    const li = document.createElement("li");
    li.textContent = `[${m.timestamp}] ${m.sender} -> ${m.receiver}: ${m.content}`;
    list.appendChild(li);
  });
  status(`Loaded ${result.json.length} messages`);
}
