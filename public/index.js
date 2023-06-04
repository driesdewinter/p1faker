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

var policies = {};
var settings = {};

function displayPolicy(index) {
    settings.active_policy = index;
    const policy = policies[index];
    for (id of ["dropDownHeadNow", "dropDownHeadLater"]) {
	    dropDownHead = document.getElementById(id);
	    dropDownHead.innerHTML = '<i class="wi ' + policy.icon + '"></i>' + policy.label;
	}
	detail = document.getElementById("detail");
	detail.innerHTML = policy.description;
	deactivateSchedule();
    for (input of detail.getElementsByClassName("number")) {
  	    input.value = settings[input.id];
  	}
}

function activatePolicy(index) {
    console.log("activatePolicy " + index);
    var req = new XMLHttpRequest();
    req.onreadystatechange = function() { 
        if (req.readyState == 4 && req.status == 204)
            displayPolicy(index);
    }
    req.open("POST", "/api/settings", true); // true for asynchronous 
    req.setRequestHeader("Content-Type", "application/json");
    req.send(JSON.stringify({active_policy: index}));
}

function setNextPolicy(index) {
    const policy = policies[index];
    console.log("setNextPolicy " + index);
    dropDownHead = document.getElementById("dropDownHeadLater");
    dropDownHead.innerHTML = '<i class="wi ' + policy.icon + '"></i>' + policy.label;
    if (index == settings.active_policy)
        deactivateSchedule();
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
                displayPolicy(settings.active_policy);
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


