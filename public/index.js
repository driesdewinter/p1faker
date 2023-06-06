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
    const next_delay = (settings.next_time - now());
    document.getElementById('next_delay').value = "" + Math.floor(next_delay/3600) + ":" + ("00"+ Math.floor(next_delay/60%60)).slice(-2);
}

function activateScheduleWithDefaultDelay() {
    setNextDelay(settings.default_next_delay)
    activateSchedule();
}

// Close the dropdown menu if the user clicks outside of it
window.onclick = function(event) {
  if (!event.target.matches('.dropbtn'))
    hideDropDowns();
}

function now() { return new Date().getTime() / 1000; } 

var policies = {};
var settings = {};

function displayPolicy() {
    const active_policy = policies[settings.active_policy];
	var dropDownHeadNow = document.getElementById("dropDownHeadNow");
	dropDownHeadNow.innerHTML = '<i class="wi ' + active_policy.icon + '"></i>' + active_policy.label;
    const next_policy = policies[settings.next_policy];
	var dropDownHeadLater = document.getElementById("dropDownHeadLater");
	dropDownHeadLater.innerHTML = '<i class="wi ' + next_policy.icon + '"></i>' + next_policy.label;
	detail = document.getElementById("detail");
	detail.innerHTML = active_policy.description;
    const next_delay = settings.next_time - now();
    if (next_delay < 0) settings.next_policy = settings.active_policy
	if (settings.active_policy == settings.next_policy)
    	deactivateSchedule();
    else
        activateSchedule();
    for (input of detail.getElementsByClassName("number")) {
  	    input.value = settings[input.id];
  	}
}

function activatePolicy(index) {
    console.log("activatePolicy " + index);
    var req = new XMLHttpRequest();
    settings.active_policy = settings.next_policy = index
    settings.next_time = now() + settings.default_next_delay
    displayPolicy();
    req.open("POST", "/api/settings", true); // true for asynchronous 
    req.setRequestHeader("Content-Type", "application/json");
    req.send(JSON.stringify(settings));
}

function setNextPolicy(index) {
    const policy = policies[index];
    console.log("setNextPolicy " + index);
    dropDownHead = document.getElementById("dropDownHeadLater");
    dropDownHead.innerHTML = '<i class="wi ' + policy.icon + '"></i>' + policy.label;
    if (index == settings.active_policy)
        deactivateSchedule();
    var req = new XMLHttpRequest();
    req.open("POST", "/api/settings", true); // true for asynchronous 
    req.setRequestHeader("Content-Type", "application/json");
    req.send(JSON.stringify({next_policy: index}));
}

function changeSetting(input) {
    const name = input.id;
    const value = parseFloat(input.value);
    console.log("changeSetting " + name + " = " + value);
    var req = new XMLHttpRequest();
    req.open("POST", "/api/settings", true); // true for asynchronous 
    req.setRequestHeader("Content-Type", "application/json");
    setting = {};
    setting[name] = value
    settings[name] = value;
    req.send(JSON.stringify(setting));
}

function setNextDelay(val) {
    console.log("changeSetting next_delay = " + val);
    var req = new XMLHttpRequest();
    req.open("POST", "/api/settings", true); // true for asynchronous 
    req.setRequestHeader("Content-Type", "application/json");
    setting = {next_time: now() + val};
    settings["next_time"] = setting.next_time;
    req.send(JSON.stringify(setting));
}

function changeNextDelay() {
    const str = document.getElementById("next_delay").value;
    var sexagesimals = str.split(":");
    var factor = 3600;
    var nextDelay = 0;
    for (sexagesimal of sexagesimals) {
        nextDelay += parseFloat(sexagesimal) * factor
        factor /= 60
    }
    console.log("Parsed " + str + " to " + nextDelay + " seconds") 
    setNextDelay(nextDelay)
}

var policiesreq = new XMLHttpRequest();
policiesreq.open("GET", "/api/policies", true); // true for asynchronous 
policiesreq.onreadystatechange = function() { 
    if (policiesreq.readyState == 4 && policiesreq.status == 200)
    {
        var dropDownListNow = document.getElementById("dropDownListNow");
        var dropDownListLater = document.getElementById("dropDownListLater");
        policylist = JSON.parse(policiesreq.responseText);
        for (const policy of policylist) {
            policies[policy.index] = policy;
	        console.log(policy.index + ": " + policy);
            dropDownListNow.insertAdjacentHTML('beforeend', 
                '<a onclick="activatePolicy(' + policy.index + ')">' +
                '<i class="wi ' + policy.icon + '"></i>' + policy.label + '</a>');
            dropDownListLater.insertAdjacentHTML('beforeend', 
                '<a onclick="setNextPolicy(' + policy.index + ')">' +
                '<i class="wi ' + policy.icon + '"></i>' + policy.label + '</a>');
        }
        
        var settingsreq = new XMLHttpRequest();
        settingsreq.open("GET", "/api/settings", true); // true for asynchronous 
        settingsreq.onreadystatechange = function() { 
            if (settingsreq.readyState == 4 && settingsreq.status == 200)
            {
                settings = JSON.parse(settingsreq.responseText);
                displayPolicy();
            }
        }
        settingsreq.send(null);
    }
};
policiesreq.send(null);


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

function refreshP1Status() {
    var req = new XMLHttpRequest();
    req.onreadystatechange = function() { 
        if (req.readyState == 4)
        {
            if (req.status == 200)
            {
                var p1status = JSON.parse(req.responseText)
                if (p1status) 
                    document.getElementById("p1status").innerHTML = ""
                else
                    document.getElementById("p1status").innerHTML = "<br/>Niet verbonden met laadpunt"
            }
            setTimeout(refreshP1Status, 2500);
        }
    }
    req.open("GET", "/api/p1status", true); // true for asynchronous 
    req.send(null);
}
refreshP1Status();


