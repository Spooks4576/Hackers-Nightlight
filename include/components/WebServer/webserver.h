#include <WiFi.h>
#include <WebServer.h>

static const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>ESP32 Management AP</title>
    <style>
        body {
            width: fit-content;
        }
        table, tr, th {
            border: 1px solid;
            border-collapse: collapse;
            text-align: center;
        }
        th, td {
            padding:10px;
        }
        tr:hover, tr.selected {
            background-color: lightblue;
            cursor:pointer;
        }
    </style>
</head>
<body onLoad="getStatus()">
    <h1>ESP32 Wi-Fi Penetration Tool</h1>
    <section id="errors"></section>
    <section id="loading">Loading... Please wait</section>
    <section id="ready" style="display: none;">
        <h2>Attack configuration</h2>
        <form onSubmit="runAttack(); return false;">
            <fieldset>
                <legend>Select target</legend>
                <table id="ap-list"></table>
                <p>
                    <button type="button" onClick="refreshAps()">Refresh</button>
                </p>
            </fieldset>
            <fieldset>
                <legend>Attack configuration</legend>
                <p>
                    <label for="attack_type">Attack type:</label>
                    <select id="attack_type" onChange="updateConfigurableFields(this)" required>
                        <option value="0" title="This type is not implemented yet." disabled>ATTACK_TYPE_PASSIVE</option>
                        <option value="1">ATTACK_TYPE_HANDSHAKE</option>
                        <option value="2" selected>ATTACK_TYPE_PMKID</option>
                        <option value="3">ATTACK_TYPE_DOS</option>
                    </select>
                </p>
                <p>
                    <label for="attack_method">Attack method:</label>
                    <select id="attack_method" required disabled>
                        <option value="" selected disabled hidden>NOT AVAILABLE</option>
                    </select>
                </p>
                <p>
                    <label for="attack_timeout">Attack timeout (seconds):</label>
                    <input type="number" min="0" max="255" id="attack_timeout" value="" required/>
                </p>
                <p>
                    <button>Attack</button>
                </p>
            </fieldset>
        </form>
    </section>
    <section id="running" style="display: none;">
        Time elapsed: <span id="running-progress"></span>
    </section>
    <section id="result" style="display: none;">
        <div id="result-meta">Loading result.. Please wait</div>
        <div id="result-content"></div>
        <button type="button" onClick="resetAttack()">New attack</button>
    </section>
    <script>
    var AttackStateEnum = { READY: 0, RUNNING: 1, FINISHED: 2, TIMEOUT: 3};
    var AttackTypeEnum = { ATTACK_TYPE_PASSIVE: 0, ATTACK_TYPE_HANDSHAKE: 1, ATTACK_TYPE_PMKID: 2, ATTACK_TYPE_DOS: 3};
    var selectedApElement = -1;
    var poll;
    var poll_interval = 1000;
    var running_poll;
    var running_poll_interval = 1000;
    var attack_timeout = 0;
    var time_elapsed = 0;
    var defaultResultContent = document.getElementById("result").innerHTML;
    var defaultAttackMethods = document.getElementById("attack_method").outerHTML;
    function getStatus() {
        var oReq = new XMLHttpRequest();
        oReq.onload = function() {
            var arrayBuffer = oReq.response;
            if(arrayBuffer) {
                var attack_state = parseInt(new Uint8Array(arrayBuffer, 0, 1));
                var attack_type = parseInt(new Uint8Array(arrayBuffer, 1, 1));
                var attack_content_size = parseInt(new Uint16Array(arrayBuffer, 2, 1));
                var attack_content = new Uint8Array(arrayBuffer, 4);
                console.log("attack_state=" + attack_state + "; attack_type=" + attack_type + "; attack_count_size=" + attack_content_size);
                var status = "ERROR: Cannot parse attack state.";
                hideAllSections();
                switch(attack_state) {
                    case AttackStateEnum.READY:
                        showAttackConfig();
                        break;
                    case AttackStateEnum.RUNNING:
                        showRunning();
                        console.log("Poll");
                        setTimeout(getStatus, poll_interval);
                        break;
                    case AttackStateEnum.FINISHED:
                        showResult("FINISHED", attack_type, attack_content_size, attack_content);
                        break;
                    case AttackStateEnum.TIMEOUT:
                        showResult("TIMEOUT", attack_type, attack_content_size, attack_content);
                        break;
                    default:
                        document.getElementById("errors").innerHTML = "Error loading attack status! Unknown state.";
                }
                return;
                
            }
        };
        oReq.onerror = function() {
            console.log("Request error");
            document.getElementById("errors").innerHTML = "Cannot reach ESP32. Check that you are connected to management AP. You might get disconnected during attack.";
            getStatus();
        };
        oReq.ontimeout = function() {
            console.log("Request timeout");
            getStatus();  
        };
        oReq.open("GET", "http://192.168.4.1/status", true);
        oReq.responseType = "arraybuffer";
        oReq.send();
    }
    function hideAllSections(){
        for(let section of document.getElementsByTagName("section")){
            section.style.display = "none";
        };
    }
    function showRunning(){
        hideAllSections();
        document.getElementById("running").style.display = "block";
    }
    function countProgress(){
        if(time_elapsed >= attack_timeout){
            document.getElementById("errors").innerHTML = "Please reconnect to management AP";
            document.getElementById("errors").style.display = "block";
            clearInterval(running_poll);
        }
        document.getElementById("running-progress").innerHTML = time_elapsed + "/" + attack_timeout + "s";
        time_elapsed++;
    }
    function showAttackConfig(){
        document.getElementById("ready").style.display = "block";
        refreshAps();
    }
    function showResult(status, attack_type, attack_content_size, attack_content){
        hideAllSections();
        clearInterval(poll);
        document.getElementById("result").innerHTML = defaultResultContent;
        document.getElementById("result").style.display = "block";
        document.getElementById("result-meta").innerHTML = status + "<br>";
        type = "ERROR: Cannot parse attack type.";
        switch(attack_type) {
            case AttackTypeEnum.ATTACK_TYPE_PASSIVE:
                type = "ATTACK_TYPE_PASSIVE";
                break;
            case AttackTypeEnum.ATTACK_TYPE_HANDSHAKE:
                type = "ATTACK_TYPE_HANDSHAKE";
                resultHandshake(attack_content, attack_content_size);
                break;
            case AttackTypeEnum.ATTACK_TYPE_PMKID:
                type = "ATTACK_TYPE_PMKID";
                resultPmkid(attack_content, attack_content_size);
                break;
            case AttackTypeEnum.ATTACK_TYPE_DOS:
                type = "ATTACK_TYPE_DOS";
                break;
            default:
                type = "UNKNOWN";
        }
        document.getElementById("result-meta").innerHTML += type + "<br>";
    }
    function refreshAps() {
        document.getElementById("ap-list").innerHTML = "Loading (this may take a while)...";
        var oReq = new XMLHttpRequest();
        oReq.onload = function() {
            document.getElementById("ap-list").innerHTML = "<th>SSID</th><th>BSSID</th><th>RSSI</th>";
            var arrayBuffer = oReq.response;
            if(arrayBuffer) {
                var byteArray = new Uint8Array(arrayBuffer);
                for  (let i = 0; i < byteArray.byteLength; i = i + 40) {
                    var tr = document.createElement('tr');
                    tr.setAttribute("id", i / 40);
                    tr.setAttribute("onClick", "selectAp(this)");
                    var td_ssid = document.createElement('td');
                    var td_rssi = document.createElement('td');
                    var td_bssid = document.createElement('td');
                    td_ssid.innerHTML = new TextDecoder("utf-8").decode(byteArray.subarray(i + 0, i + 32));
                    tr.appendChild(td_ssid);
                    for(let j = 0; j < 6; j++){
                        td_bssid.innerHTML += uint8ToHex(byteArray[i + 33 + j]) + ":";
                    }
                    tr.appendChild(td_bssid);
                    td_rssi.innerHTML = byteArray[i + 39] - 255;
                    tr.appendChild(td_rssi);
                    document.getElementById("ap-list").appendChild(tr);
                }
            }
        };
        oReq.onerror = function() {
            document.getElementById("ap-list").innerHTML = "ERROR";
        };
        oReq.open("GET", "http://192.168.4.1/ap-list", true);
        oReq.responseType = "arraybuffer";
        oReq.send();
    }
    function selectAp(el) {
        console.log(el.id);
        if(selectedApElement != -1){
            selectedApElement.classList.remove("selected")
        }
        selectedApElement=el;
        el.classList.add("selected");
    }
    function runAttack() {
        if(selectedApElement == -1){
            console.log("No AP selected. Attack not started.");
            document.getElementById("errors").innerHTML = "No AP selected. Attack not started.";
            return;
        }
        hideAllSections();
        document.getElementById("running").style.display = "block";
        var arrayBuffer = new ArrayBuffer(4);
        var uint8Array = new Uint8Array(arrayBuffer);
        uint8Array[0] = parseInt(selectedApElement.id);
        uint8Array[1] = parseInt(document.getElementById("attack_type").value);
        uint8Array[2] = parseInt(document.getElementById("attack_method").value);
        uint8Array[3] = parseInt(document.getElementById("attack_timeout").value);
        var oReq = new XMLHttpRequest();
        oReq.open("POST", "http://192.168.4.1/run-attack", true);
        oReq.send(arrayBuffer);
        getStatus();
        attack_timeout = parseInt(document.getElementById("attack_timeout").value);
        time_elapsed = 0;
        running_poll = setInterval(countProgress, running_poll_interval);
    }
    function resetAttack(){
        hideAllSections();
        showAttackConfig();
        var oReq = new XMLHttpRequest();
        oReq.open("HEAD", "http://192.168.4.1/reset", true);
        oReq.send();
    }
    function resultPmkid(attack_content, attack_content_size){
        var mac_ap = "";
        var mac_sta = "";
        var ssid = "";
        var ssid_text = "";
        var pmkid = "";
        var index = 0;
        for(let i = 0; i < 6; i = i + 1) {
            mac_ap += uint8ToHex(attack_content[index + i]);
        }
        index = index + 6;
        for(let i = 0; i < 6; i = i + 1) {
            mac_sta += uint8ToHex(attack_content[index + i]);
        }
        index = index + 6;
        for(let i = 0; i < attack_content[index]; i = i + 1) {
            ssid += uint8ToHex(attack_content[index + 1 + i]);
            ssid_text += String.fromCharCode(attack_content[index + 1 + i]);
        }
        index = index + attack_content[index] + 1;
        var pmkid_cnt = 0;
        for(let i = 0; i < attack_content_size - index; i = i + 1) {
            if((i % 16) == 0){
                pmkid += "<br>";
                pmkid += "</code>PMKID #" + pmkid_cnt + ": <code>";
                pmkid_cnt += 1;
            }
            pmkid += uint8ToHex(attack_content[index + i]);
        }
        document.getElementById("result-content").innerHTML = "";
        document.getElementById("result-content").innerHTML += "MAC AP: <code>" + mac_ap + "</code><br>";
        document.getElementById("result-content").innerHTML += "MAC STA: <code>" + mac_sta + "</code><br>";
        document.getElementById("result-content").innerHTML += "(E)SSID: <code>" + ssid + "</code> (" + ssid_text + ")";
        document.getElementById("result-content").innerHTML += "<code>" + pmkid + "</code><br>";
        document.getElementById("result-content").innerHTML += "<br>Hashcat ready format:"
        document.getElementById("result-content").innerHTML += "<code>" + pmkid + "*" + mac_ap + "*" + mac_sta  + "*" + ssid  + "</code><br>";
    }
    function resultHandshake(attack_content, attack_content_size){
        document.getElementById("result-content").innerHTML = "";
        var pcap_link = document.createElement("a");
        pcap_link.setAttribute("href", "capture.pcap");
        pcap_link.text = "Download PCAP file";
        var hccapx_link = document.createElement("a");
        hccapx_link.setAttribute("href", "capture.hccapx");
        hccapx_link.text = "Download HCCAPX file";
        document.getElementById("result-content").innerHTML += "<p>" + pcap_link.outerHTML + "</p>";
        document.getElementById("result-content").innerHTML += "<p>" + hccapx_link.outerHTML + "</p>";
        var handshakes = "";
        for(let i = 0; i < attack_content_size; i = i + 1) {
            handshakes += uint8ToHex(attack_content[i]);
            if(i % 50 == 49) {
                handshakes += "\n";
            }
        }
        document.getElementById("result-content").innerHTML += "<pre><code>" + handshakes + "</code></pre>";
    }
    function uint8ToHex(uint8){
        return ("00" + uint8.toString(16)).slice(-2);
    }
    function updateConfigurableFields(el){
        document.getElementById("attack_method").outerHTML = defaultAttackMethods;
        switch(parseInt(el.value)){
            case AttackTypeEnum.ATTACK_TYPE_PASSIVE:
                console.log("PASSIVE configuration");
                break;
            case AttackTypeEnum.ATTACK_TYPE_HANDSHAKE:
                console.log("HANDSHAKE configuration");
                document.getElementById("attack_timeout").value = 60;
                setAttackMethods(["DEAUTH_ROGUE_AP (PASSIVE)", "DEAUTH_BROADCAST (ACTIVE)", "CAPTURE_ONLY (PASSIVE)"]);
                break;
            case AttackTypeEnum.ATTACK_TYPE_PMKID:
                console.log("PMKID configuration");
                document.getElementById("attack_timeout").value = 5;
                break;
            case AttackTypeEnum.ATTACK_TYPE_DOS:
                console.log("DOS configuration");
                document.getElementById("attack_timeout").value = 120;
                setAttackMethods(["DEAUTH_ROGUE_AP (PASSIVE)", "DEAUTH_BROADCAST (ACTIVE)", "DEAUTH_COMBINE_ALL"]);
                break;
            default:
                console.log("Unknown attack type");
                break;
        }
    }
    function setAttackMethods(attackMethodsArray){
        document.getElementById("attack_method").removeAttribute("disabled");
        attackMethodsArray.forEach(function(method, index){
            let option = document.createElement("option");
            option.value = index;
            option.text = method;
            option.selected = true;
            document.getElementById("attack_method").appendChild(option);
        });
    }
    </script>
</body>
</html>
)rawliteral";

class WebServerComponent {
private:
    WebServer server;
    const char* ssid;
    const char* password;
    IPAddress ip;

public:
    WebServerComponent(const char* ssid, const char* password) : ssid(ssid), password(password) {}

    void begin();

    void handleRoot();

    void handleNotFound();

    void handleRequests();

    void HandleStatus();

    void HandlePcap();

    void HandleHccapx();

    void HandleAPList();
};