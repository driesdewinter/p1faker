function hideDropDowns() {
    for (id of ["dropDownListNow", "dropDownListLater"]) {
        document.getElementById(id).classList.remove('show');
    }
}

/* When the user clicks on the button,
toggle between hiding and showing the dropdown content */
function showDropDown(id) {
    const visible = document.getElementById(id).classList.contains('show');
    hideDropDowns();
    if (!visible)
        document.getElementById(id).classList.add("show");
}

function deactivateSchedule() {
    document.getElementById('schedule-on').classList.remove("show");
    document.getElementById('schedule-off').classList.add("show");
}

function activateSchedule() {
    document.getElementById('schedule-off').classList.remove("show");
    document.getElementById('schedule-on').classList.add("show");
}

// Close the dropdown menu if the user clicks outside of it
window.onclick = function(event) {
  if (!event.target.matches('.dropbtn'))
    hideDropDowns();
}

var policies = {}
var active_policy = "";

function displayPolicy(name) {
    active_policy = name;
    const policy = policies[name];
    for (id of ["dropDownHeadNow", "dropDownHeadLater"]) {
	    dropDownHead = document.getElementById(id);
	    dropDownHead.innerHTML = '<i class="wi ' + policy.icon + '"></i>' + policy.label;
	}
	detail = document.getElementById("detail");
	detail.innerHTML = policy.description;
	deactivateSchedule();
}

function activatePolicy(a) {
    const name = a.id;
    console.log("activatePolicy " + name);
    var req = new XMLHttpRequest();
    req.onreadystatechange = function() { 
        if (req.readyState == 4 && req.status == 200)
            displayPolicy(name);
    }
    req.open("POST", "/api/activate_policy", true); // true for asynchronous 
    req.setRequestHeader("Content-Type", "application/json");
    req.send(JSON.stringify(name));
}

function setNextPolicy(a) {
    const name = a.id;
    const policy = policies[name];
    console.log("setNextPolicy " + name);
    dropDownHead = document.getElementById("dropDownHeadLater");
    dropDownHead.innerHTML = '<i class="wi ' + policy.icon + '"></i>' + policy.label;
    if (name == active_policy)
        deactivateSchedule();
}

var initreq = new XMLHttpRequest();
initreq.onreadystatechange = function() {
    if (initreq.readyState == 4 && initreq.status == 200)
    {
        dropDownListNow = document.getElementById("dropDownListNow");
        dropDownListLater = document.getElementById("dropDownListLater");
        obj = JSON.parse(initreq.responseText);
        policies = obj.policies;
        for (const name in policies) {
        	const policy = policies[name];
        	console.log(name + ": " + policy);
            dropDownListNow.insertAdjacentHTML('beforeend', 
                '<a onclick="activatePolicy(this)" id="' + name + '">' +
                '<i class="wi ' + policy.icon + '"></i>' + policy.label + '</a>');
            dropDownListLater.insertAdjacentHTML('beforeend', 
                '<a onclick="setNextPolicy(this)" id="' + name + '">' +
                '<i class="wi ' + policy.icon + '"></i>' + policy.label + '</a>');
        }
        displayPolicy(obj.active_policy);
    }
}
initreq.open("GET", "/api/policies", true); // true for asynchronous 
initreq.send(null);

function refresh() {
    var req = new XMLHttpRequest();
    req.onreadystatechange = function() { 
        if (req.readyState == 4)
        {
            if (req.status == 200)
                document.getElementById("curcap").innerHTML = req.responseText;
            setTimeout(refresh, 1000);
        }
    }
    req.open("GET", "/api/curcap", true); // true for asynchronous 
    req.send(null);
}
refresh();


