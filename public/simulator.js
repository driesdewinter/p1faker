var params = null

function changeParam(input) {
    const key = input.id;
    const value = parseInt(input.value);
    console.log("changeSetting " + name + " = " + value);
    params[key] = value;
    var req = new XMLHttpRequest();
    req.open("POST", "/api/simulator/input", true); // true for asynchronous 
    req.setRequestHeader("Content-Type", "application/json");
    req.send(JSON.stringify(params));
}

function refreshOutput() {
    var req = new XMLHttpRequest();
    req.onreadystatechange = function() { 
        if (req.readyState == 4)
        {
            if (req.status == 200)
            {
                output = JSON.parse(req.responseText)
                for (const key in output) {
                    var td = document.getElementById(key)
                    td.innerText = output[key]
                }
            }
            setTimeout(refreshOutput, 1000);
        }
    }
    req.open("GET", "/api/simulator/output", true); // true for asynchronous 
    req.send(null);
}

var loadParamsReq = new XMLHttpRequest();
loadParamsReq.onreadystatechange = function() { 
    if (loadParamsReq.readyState == 4 && loadParamsReq.status == 200)
    {
        params = JSON.parse(loadParamsReq.responseText)
        var table = document.getElementById("table")
        for (const key in params) {
            table.insertAdjacentHTML('beforeend', ''
                + '<tr><td>' + key + '</td><td>'
                + '<input id="' + key + '" onchange="changeParam(this)"></input>'
                + '</td></tr>')
        }
        for (const key in params) {
            var input = document.getElementById(key)
            input.value = params[key]
        }
        
        var loadOutputReq = new XMLHttpRequest();
        loadOutputReq.onreadystatechange = function() { 
            if (loadOutputReq.readyState == 4 && loadOutputReq.status == 200)
            {
                output = JSON.parse(loadOutputReq.responseText)
                var table = document.getElementById("table")
                for (const key in output) {
                    table.insertAdjacentHTML('beforeend', '<tr><td>' + key + '</td><td id="' + key + '"></td></tr>')
                }
                refreshOutput()
            }
        }
        loadOutputReq.open("GET", "/api/simulator/output", true); // true for asynchronous 
        loadOutputReq.send(null)
    }
}
loadParamsReq.open("GET", "/api/simulator/input", true); // true for asynchronous 
loadParamsReq.send(null);



