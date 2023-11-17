#include <math.h>

// FOR THE INTERNET STUFF
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// for the MAX SENSOR
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include "spo2_algorithm.h"
#define samples 3
#define MAX_BRIGHTNESS 255
MAX30105 particleSensor;

// ================ TEMPERATURE =================
float temperatureC(int value, float R1 = 5000){
  float logR2, R2, T, Tc, Tf;
  float c1 = 1.009249522e-03, c2 = 2.378405444e-04, c3 = 2.019202697e-07;
  R2 = R1 * (4095.0 / (float)value - 1.0);
  logR2 = log(R2);
  T = (1.0 / (c1 + c2*logR2 + c3*logR2*logR2*logR2));
  Tc = T - 273.15;
  Tf = (Tc * 9.0)/ 3.3 + 32.0;
  return Tf;
}
float temperature;

// ================== HEARTBEAT ==================
float spo2;

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
//Arduino Uno doesn't have enough SRAM to store 100 samples of IR led data and red led data in 32-bit format
//To solve this problem, 16-bit MSB of the sampled data will be truncated. Samples become 16-bit data.
uint16_t irBuffer[100]; //infrared LED sensor data
uint16_t redBuffer[100];  //red LED sensor data
#else
uint32_t irBuffer[100]; //infrared LED sensor data
uint32_t redBuffer[100];  //red LED sensor data
#endif

int32_t bufferLength; //data length
int32_t spo22; //SPO2 value
int8_t validSPO2; //indicator to show if the SPO2 calculation is valid
int32_t heartRate; //heart rate value
int8_t validHeartRate; //indicator to show if the heart rate calculation is valid

int count_ir = 0;
int count_red = 0;

// variables for the red measure
uint32_t max_red = 0;
uint32_t min_red = 10000000;
uint32_t red_sampled[samples] = { 0 };
uint32_t red_value = 0;
float red_avg = 0;

// variables for the ir measure
uint32_t max_ir = 0;
uint32_t min_ir = 10000000;
uint32_t ir_sampled[samples] = { 0 };
uint32_t ir_value = 0;
float ir_avg = 0;


// ==================== WIFI ====================
// LOAD THE HTML PAGE
String htmlPage = R"(
    <!DOCTYPE html>
<html lang="en">

<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Sistema de Monitoreo de Signos Vitales</title>

    <script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/2.9.4/Chart.js"></script>

    <script src="http://cdnjs.cloudflare.com/ajax/libs/raphael/2.1.0/raphael-min.js"> </script>

    <style>
        /* add some restrictions for mobile first */
        :root {
            --min-width: 500px;
            --max-width: 700px;
        }

        /* add some cool styles for the whole document */
        body {
            font-family: Arial, Helvetica, sans-serif;
            text-align: center;
            background-color: #f5f5f5;
            /* avoid y scroll */
        }

        h1 {
            font-size: 2.5em;
            color: #0173b5;
        }

        h2 {
            color: #00a2ff;
        }

        h3 {
            color: #61acd8;
        }

        #variables_input>div,
        #variable_output>div,
        #result {
            color: #61acd8;
            /* set size as h2 */
            font-size: 1.1em;
            /* set style as h2 */
            font-weight: bold;
        }

        /* set the format of the table */
        #variables_input>div>table,
        #variable_output>div>table {
            /* change style to another font */
            font-family: Arial, Helvetica, sans-serif;
            /* use style and size as paragraph */
            font-size: 0.7em !important;
            font-weight: normal;
            color: #000;
        }

        .fuzzy {
            margin: 0 auto;
            display: block;
            max-width: var(--min-width);
        }

        /* make the fuzzy block centered */
        #variables_input,
        #variable_output,
        #inferences,
        #input,
        #output {
            clear: both;
            /* make each div into different lines */
            margin: 0 auto;
            /* center the div */
            display: block;
            /* make the div as a block */
            text-align: center;
            width: 100%;
        }

        img {
            max-width: var(--min-width);
        }

        canvas {
            width: 100%;
            height: auto;
            /* use a max width of a mobile first app */
            min-width: var(--min-width);
            /* max-width: var(--max-width); */
            /* background-color: #000; */
        }

        .graphs {
            display: flex;
            justify-content: space-evenly;
            flex-wrap: wrap;
            /* size details */
            margin-top: 2em;
            place-items: center;
        }

        .plot {
            /* cool pastel color */
            background-color: #f2f2f2;
            border: 1px solid #00a2ff;
            /* max-width: var(--max-width); */
            min-width: var(--min-width);
            margin: 1em 0;
        }

        /* make a border to the averages of the same size */
        .averages {
            border: 1px solid #00a2ff;
            /* padding: 1em; */
            padding-bottom: 1em;
            margin: 1em 0;
            min-width: var(--min-width);
            min-height: calc(var(--max-width) / 6);
        }

        /* give an style to the result text */
        #result {
            font-size: 1.5em;
            font-weight: bold;
            color: #00a2ff;
        }
    </style>
</head>

<body onload="presentation() ">

    <!-- add a title -->
    <h1>Sistema de Monitoreo de Signos Vitales</h1>

    <!-- add a image as top banner -->
    <img src="https://raw.githubusercontent.com/fcomovaz/practicas/main/ESP32_WEB_DATA/evolve.JPG?token="
        alt="EVOLVE ENERGY">

    <!-- add a list of data -->
    <!-- create a div to my graphs -->
    <div class="graphs">

        <div class="plot temperature">
            <canvas id="temperature" style="width:100%;max-width:700px"></canvas>
        </div>
        <div class="plot bpm">
            <canvas id="bpm" style="width:100%;max-width:700px"></canvas>
        </div>
        <div class="plot spo2">
            <canvas id="spo2" style="width:100%;max-width:700px"></canvas>
        </div>
    </div>


    <h1>Estadísticas Promediadas</h1>

    <div class="graphs">

        <div class="averages">
            <h2>Temperature Average</h2>
            <div id="average_temp"></div>
        </div>
        <div class="averages">
            <h2>BPM Average</h2>
            <div id="average_bpm"></div>
        </div>
        <div class="averages">
            <h2>SPO2 Average</h2>
            <div id="average_spo2"></div>
        </div>
    </div>

    <h1>Predicción Difusa</h1>

    <!-- ADD IN BODY -->

    <div class="fuzzy">

        <h2>Input Variables:</h2>
        <div id="variables_input"></div>
        <hr />
        <h2>Output Variables:</h2>
        <div id="variable_output"></div>
        <hr />
        <h2>Inferences:</h2>
        <div id="inferences"></div>
        <hr />
        <h2 id="output">Result:</h2>
        <div id="input"></div>
        <!-- <input type="button" value="Calculate" onClick=" document.getElementById( 'result' ).innerHTML='Result: '+fl.getResult(obj); "> -->
        <!-- <input type="button" value="Calculate" onClick=" getValuesAvg(); "> -->
        <span id="result"></span>
        <div id="draw_result"></div>
        <hr>
    </div>
    <script>

        function getValuesAvg() {
            // take the values from the averages and put them in the crisp_input
            obj.crisp_input[0] = parseFloat(document.getElementById('average_spo2').innerHTML);
            obj.crisp_input[1] = parseFloat(document.getElementById('average_bpm').innerHTML);
            obj.crisp_input[2] = parseFloat(document.getElementById('average_temp').innerHTML);
            var a = fl.getResult(obj);
            var fz_str = "";
            if (a < 20) {
                fz_str = "Bad";
            } else if (a < 40) {
                fz_str = "Normal";
            } else if (a < 60) {
                fz_str = "Acceptable";
            } else if (a < 80) {
                fz_str = "Dangerous";
            } else {
                fz_str = "Critical";
            }
            document.getElementById('result').innerHTML = fz_str;
        }

        var FuzzyLogic = function () { "use strict"; var a = function () { }; return a.prototype = { getResult: function (a) { var b = this.construct(a.variables_input), c = this.fuzzification(a.crisp_input, b), d = this.output_combination(c, a.inferences, a.variable_output), e = this.takeMaxOfArraySet(d), f = this.defuzzification(e, this.construct_variable(a.variable_output.sets)); return f }, construct: function (a) { var c, b = []; for (c = a.length - 1; c >= 0; c -= 1)b[c] = this.construct_variable(a[c].sets); return b }, construct_variable: function (a) { var c, b = []; for (c = a.length - 1; c >= 0; c -= 1)b[c] = { a: a[c], firstPoint: a[c][0] === a[c][1] ? 1 : 0, lastPoint: a[c][2] === a[c][3] ? 1 : 0, mUp: 1 / (a[c][1] - a[c][0]), mDown: 1 / (a[c][3] - a[c][2]) }; return b }, fuzzification: function (a, b) { var d, c = []; for (d = b.length - 1; d >= 0; d -= 1)c[d] = this.fuzzification_variable(a[d], b[d]); return c }, fuzzification_variable: function (a, b) { var d, c = []; for (d = b.length - 1; d >= 0; d -= 1)c[d] = this.fuzzification_function(a, b[d]); return c }, fuzzification_function: function (a, b) { var c = 0; return b.a[0] >= a ? c = b.firstPoint : b.a[1] > a ? c = b.mUp * (a - b.a[0]) : b.a[2] >= a ? c = 1 : b.a[3] > a ? c = 1 - b.mDown * (a - b.a[2]) : a >= b.a[3] && (c = b.lastPoint), c }, output_combination: function (a, b, c) { var e, f, d = []; for (e = c.sets.length - 1; e >= 0; e -= 1)d[e] = []; for (e = b.length - 1; e >= 0; e -= 1)for (f = b[e].length - 1; f >= 0; f -= 1)b[e][f] >= 0 && d[b[e][f]].push(a[e][f]); return d }, defuzzification: function (a, b) { var e, f, g, h, i, j, k, l, m, n, o, p, q, r, c = 0, d = 0; for (e = a.length - 1; e >= 0; e -= 1)f = b[e], g = f.a, h = a[e], i = g[3] - g[0], k = g[0], g[0] !== g[1] && (k += h / f.mUp), l = g[3], g[2] !== g[3] && (l -= h / f.mDown), m = 0, g[0] !== k && (m += (k - g[0]) * a[e] / 2), k !== l && (m += (l - k) * a[e]), l !== g[3] && (m += (g[3] - l) * a[e] / 2), j = l - k, n = h / 3 * (i + 2 * j) / (j + i), p = k + (l - k) / 2, q = g[0] + (g[3] - g[0]) / 2, r = 0, 0 !== p - q && (r = h / (p - q)), o = q, 0 !== r && (o += n / r), c += m * o, d += m; return 0 === d ? 0 : c / d }, takeMaxOfArraySet: function (a) { var c, b = []; for (c = a.length - 1; c >= 0; c -= 1)b[c] = this.takeMaxOfArray(a[c]); return b }, takeMaxOfArray: function (a) { var c, b = a[0]; for (c = 1; a.length > c; c += 1)b = a[c] > b ? a[c] : b; return b } }, a }();

        function presentation() {
            var html = '<table border="0">';
            for (var i = 0; i < obj.inferences.length; i += 1)
                for (var j = 0; j < obj.inferences[i].length; j += 1)
                    html += '<tr><td><b>IF</b> [' + obj.variables_input[i].name + '].' + obj.variables_input[i].setsName[j] + '</td><td><b>THEN</b></td><td>' + obj.variable_output.setsName[obj.inferences[i][j]] + '</td></tr>';
            html += '</table>';
            document.getElementById('inferences').innerHTML = html;

            // var html = '';
            // for (var i = obj.variables_input.length - 1; i >= 0; i -= 1)
            //     html += obj.variables_input[i].name + ': <input type="text" value="' + obj.crisp_input[i] + '" onChange="obj.crisp_input[' + i + ']=parseFloat(this.value);"><br>';
            // document.getElementById('input').innerHTML = html;
            rap();
        }

        var paper = [];

        function rap() {
            for (var i = 0; i < obj.variables_input.length; i += 1) {
                var v = obj.variables_input[i];
                document.getElementById("variables_input").innerHTML += "<div id='variable_" + i + "'>" + v.name + "<table><tr><td id='variable_" + i + "_rap'></td><td id='variable_" + i + "_info'></td></div>";
                draw_variable(v, i);

            }
            var i = 1000;
            document.getElementById("variable_output").innerHTML += "<div id='variable_" + i + "'>" + obj.variable_output.name + "<table><tr><td id='variable_" + i + "_rap'></td><td id='variable_" + i + "_info'></td></div>";
            draw_variable(obj.variable_output, i);

        }

        function draw_variable(v, i) {
            var colors = ["#f00", "#0f0", "#00f", "#f0f", "#0ff", "#ff0", "#999"];
            paper[i] = Raphael(document.getElementById("variable_" + i + "_rap"), 400, 200);
            paper[i].path("M0 200L400 200");
            var min = v.sets[0][0];
            var max = v.sets[v.sets.length - 1][v.sets[v.sets.length - 1].length - 1];
            for (var j = 0; j < v.sets.length; j += 1) {
                document.getElementById("variable_" + i + "_info").innerHTML += v.setsName[j] + ": " + v.sets[j] + "<br>";
                draw_function(v.sets[j], i, 400 / (max - min), colors[j]);
            }

        }

        function draw_function(s, i, ratio, color) {
            draw_path(s, i, ratio, color).attr({ "stroke": color, "stroke-width": 2 });
            draw_path(s, i, ratio, color).attr({ fill: color, opacity: .3 });
        }

        function draw_path(s, i, ratio, color) {
            return paper[i].path("M" + parseInt(ratio * s[0]) + " 200L" + parseInt(ratio * s[1]) + " 0L" + parseInt(ratio * s[2]) + " 0L" + parseInt(ratio * s[3]) + " 200");
        }

        var obj = {
            crisp_input: [100, 100, 40],
            variables_input: [
                {
                    name: "SPO2",
                    setsName: ["Bad", "Critical", "Dangerous", "Acceptable", "Normal"],
                    sets: [
                        [0, 0, 50, 60],
                        [60, 70, 80, 90],
                        [90, 92, 92, 94],
                        [94, 95, 95, 96],
                        [96, 98, 100, 100]
                    ]
                },
                {
                    name: "BPM",
                    setsName: ["Bad", "Normal", "Acceptable", "Dangerous", "Critical"],
                    sets: [
                        [0, 0, 50, 75],
                        [75, 81, 94, 100],
                        [100, 105, 105, 110],
                        [110, 120, 120, 130],
                        [130, 150, 200, 200]
                    ]
                },
                {
                    name: "Temperature",
                    setsName: ["Bad", "Normal", "Acceptable", "Dangerous", "Critical"],
                    sets: [
                        [0, 0, 20, 25],
                        [25, 27, 33, 35],
                        [35, 36, 37, 38],
                        [38, 38.5, 38.5, 39],
                        [39, 41, 49, 49]
                    ]
                },
            ],
            variable_output: {
                name: "Risk",
                setsName: ["Bad", "Normal", "Acceptable", "Dangerous", "Critical"],
                sets: [
                    // divide the ranges each 20
                    [0, 0, 15, 20],
                    [20, 25, 35, 40],
                    [40, 45, 55, 60],
                    [60, 65, 75, 80],
                    [80, 85, 100, 100]
                ]
            },
            inferences: [
                [0, 4, 3, 2, 1],
                [0, 1, 2, 3, 4],
                [0, 1, 2, 3, 4],
            ]
        };
        var fl = new FuzzyLogic();


        // fill the average values with 0.0
        // document.getElementById('average_temp').innerHTML = 0.0;
        // document.getElementById('average_bpm').innerHTML = 0.0;
        // document.getElementById('average_spo2').innerHTML = 0.0;


        // specify the limit
        var limit = 10;

        // ALL ELECTRONIC AJAX PART
        // TEMPERATURE SENSOR
        var currentIndex2 = 0;
        // create an empty array with 10 elements
        var last_values2 = new Array(limit).fill(0);
        // Function to update the analog value
        function updateTemperature() {
            // Send an AJAX request to get the latest analog value
            var xhttp = new XMLHttpRequest();
            xhttp.onreadystatechange = function () {
                if (this.readyState == 4 && this.status == 200) {
                    // Update the paragraph with the received analog value
                    // Parse the received value as an integer
                    var sensorValue = parseInt(this.responseText);
                    // var sensorValue = parseFloat(this.responseText);

                    // Add the new value to the array
                    last_values2[currentIndex2] = sensorValue;

                    // Increment the current index and wrap around if needed
                    currentIndex2 = (currentIndex2 + 1) % limit;

                    // here plot
                    var xyValues = new Array(limit).fill(0);
                    var x_val = new Array(limit).fill(0);
                    var y_val = new Array(limit).fill(0);
                    for (var i = 0; i < limit; i++) {
                        x_val[i] = i;
                        y_val[i] = last_values2[i];
                        xyValues[i] = { x: x_val[i], y: y_val[i] };
                    }
                    const average_temp = array => array.reduce((a, b) => a + b) / array.length;
                    document.getElementById('average_temp').innerHTML = average_temp(y_val);


                    new Chart("temperature", {
                        type: "line",
                        data: {
                            labels: x_val,
                            datasets: [{
                                pointRadius: 4,
                                pointBackgroundColor: "rgb(0,0,255) ",
                                data: y_val,
                            }],
                        },
                        options: {
                            title: { // Add a title configuration
                                display: true, // Set to true to display the title
                                text: 'Temperature Sensor', // Your title text here
                                fontSize: 16, // Optional: Adjust the font size
                                fontStyle: 'bold', // Optional: Set font style (e.g., 'normal', 'italic', 'bold')
                            },

                            legend: { // Add a legend configuration
                                display: false, // Set to false to hide the legend
                            },
                            // Rest of your options...
                            scales: {
                                xAxes: [{
                                    ticks: {
                                        min: 0,
                                        max: limit - 1,
                                        stepSize: 1,
                                    },
                                }],
                                yAxes: [{
                                    ticks: {
                                        min: 0,
                                        max: 40,
                                        stepSize: 10,
                                    },
                                }],
                            },
                            animation: {
                                duration: 0,
                            },
                        },
                    });
                }
            };
            xhttp.open("GET", "/temp", true);
            xhttp.send();
            // close the request
            xhttp.close;
        }


        // SPO2 SENSOR
        var currentIndex3 = 0;
        // create an empty array with 10 elements
        var last_values3 = new Array(limit).fill(0);
        // Function to update the analog value
        function updateSPO2() {
            // Send an AJAX request to get the latest analog value
            var xhttp = new XMLHttpRequest();
            xhttp.onreadystatechange = function () {
                if (this.readyState == 4 && this.status == 200) {
                    // Update the paragraph with the received analog value
                    // Parse the received value as an integer
                    var sensorValue = parseInt(this.responseText);
                    // var sensorValue = parseFloat(this.responseText);

                    // Add the new value to the array
                    last_values3[currentIndex3] = sensorValue;

                    // Increment the current index and wrap around if needed
                    currentIndex3 = (currentIndex3 + 1) % limit;

                    // here plot
                    var xyValues = new Array(limit).fill(0);
                    var x_val = new Array(limit).fill(0);
                    var y_val = new Array(limit).fill(0);
                    for (var i = 0; i < limit; i++) {
                        x_val[i] = i;
                        y_val[i] = last_values3[i];
                        xyValues[i] = { x: x_val[i], y: y_val[i] };
                    }

                    const average_spo2 = array => array.reduce((a, b) => a + b) / array.length;
                    document.getElementById('average_spo2').innerHTML = average_spo2(y_val);

                    new Chart("spo2", {
                        type: "line",
                        data: {
                            labels: x_val,
                            datasets: [{
                                pointRadius: 4,
                                pointBackgroundColor: "rgb(0,0,255) ",
                                data: y_val,
                            }],
                        },
                        options: {
                            title: { // Add a title configuration
                                display: true, // Set to true to display the title
                                text: 'SPO2 Sensor', // Your title text here
                                fontSize: 16, // Optional: Adjust the font size
                                fontStyle: 'bold', // Optional: Set font style (e.g., 'normal', 'italic', 'bold')
                            },

                            legend: { // Add a legend configuration
                                display: false, // Set to false to hide the legend
                            },
                            // Rest of your options...
                            scales: {
                                xAxes: [{
                                    ticks: {
                                        min: 0,
                                        max: limit - 1,
                                        stepSize: 1,
                                    },
                                }],
                                yAxes: [{
                                    ticks: {
                                        min: 0,
                                        max: 100,
                                        stepSize: 25,
                                    },
                                }],
                            },
                            animation: {
                                duration: 0,
                            },
                        },
                    });
                }
            };
            xhttp.open("GET", "/spo2", true);
            xhttp.send();
            // close the request
            xhttp.close;
        }

        // BPM SENSOR
        var currentIndex4 = 0;
        // create an empty array with 10 elements
        var last_values4 = new Array(limit).fill(0);
        // Function to update the analog value
        function updateBPM() {
            // Send an AJAX request to get the latest analog value
            var xhttp = new XMLHttpRequest();
            xhttp.onreadystatechange = function () {
                if (this.readyState == 4 && this.status == 200) {
                    // Update the paragraph with the received analog value
                    // Parse the received value as an integer
                    var sensorValue = parseInt(this.responseText);
                    // var sensorValue = parseFloat(this.responseText);

                    // Add the new value to the array
                    last_values4[currentIndex4] = sensorValue;

                    // Increment the current index and wrap around if needed
                    currentIndex4 = (currentIndex4 + 1) % limit;

                    // here plot
                    var xyValues = new Array(limit).fill(0);
                    var x_val = new Array(limit).fill(0);
                    var y_val = new Array(limit).fill(0);
                    for (var i = 0; i < limit; i++) {
                        x_val[i] = i;
                        y_val[i] = last_values4[i];
                        xyValues[i] = { x: x_val[i], y: y_val[i] };
                    }

                    const average_bpm = array => array.reduce((a, b) => a + b) / array.length;
                    document.getElementById('average_bpm').innerHTML = average_bpm(y_val);

                    new Chart("bpm", {
                        type: "line",
                        data: {
                            labels: x_val,
                            datasets: [{
                                pointRadius: 4,
                                pointBackgroundColor: "rgb(0,0,255) ",
                                data: y_val,
                            }],
                        },
                        options: {
                            title: { // Add a title configuration
                                display: true, // Set to true to display the title
                                text: 'BPM Sensor', // Your title text here
                                fontSize: 16, // Optional: Adjust the font size
                                fontStyle: 'bold', // Optional: Set font style (e.g., 'normal', 'italic', 'bold')
                            },

                            legend: { // Add a legend configuration
                                display: false, // Set to false to hide the legend
                            },
                            // Rest of your options...
                            scales: {
                                xAxes: [{
                                    ticks: {
                                        min: 0,
                                        max: limit - 1,
                                        stepSize: 1,
                                    },
                                }],
                                yAxes: [{
                                    ticks: {
                                        min: 0,
                                        max: 150,
                                        stepSize: 25,
                                    },
                                }],
                            },
                            animation: {
                                duration: 0,
                            },
                        },
                    });
                }
            };
            xhttp.open("GET", "/bpm", true);
            xhttp.send();
            // close the request
            xhttp.close;
        }

        // Update the analog value every 100ms
        // set time interval
        var interval = 100;
        setInterval(updateSPO2, interval);
        setInterval(updateTemperature, interval);
        setInterval(updateBPM, interval);
        setInterval(getValuesAvg, interval);


        // ALL ELECTRONIC AJAX PART


    </script>


</body>


</html>
)";

const char *ssid = "Red";
const char *password = "admin1234";

WebServer server(80);

void handleRoot()
{
    server.send(200, "text/html", htmlPage); // Send the HTML page as a response
}

void handleSPO2()
{
    // int touchValue = touchRead(2);
    server.send(200, "text/plain", String(spo2));
}

void handleTemp()
{
    server.send(200, "text/plain", String(temperature));
}

void handleBPM()
{
    server.send(200, "text/plain", String(heartRate));
}

void handleNotFound()
{
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++)
    {
        message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
}

void setup(void)
{
  Serial.begin(115200);

  // ================== HEARTBEAT ==================
  Serial.println("Initializing...");

  // Initialize sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) // Use default I2C port, 400kHz speed
  {
      Serial.println("MAX30105 was not found. Please check wiring/power. ");
      while (1)
          ;
  }
  Serial.println("Place your index finger on the sensor with steady pressure.");

  // ==================== WIFI ====================
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.println("");

  while (WiFi.status() != WL_CONNECTED)
  {
      delay(500);
      Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  if (MDNS.begin("esp32"))
  {
      Serial.println("MDNS responder started");
  }

  server.on("/", handleRoot);

  server.on("/temp", handleTemp);
  server.on("/bpm", handleBPM);
  server.on("/spo2", handleSPO2);

  server.on("/inline", []()
            { server.send(200, "text/plain", "this works as well"); });
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");

  // ================== HEARTBEAT ==================
  byte ledBrightness = 60; //Options: 0=Off to 255=50mA
  byte sampleAverage = 1; //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  byte sampleRate = 3200; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //Options: 69, 118, 215, 411
  int adcRange = 4096; //Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings
  // particleSensor.setup(); // Configure sensor with default settings
  particleSensor.setPulseAmplitudeRed(0x0A); // Turn Red LED to low to indicate sensor is running
  particleSensor.setPulseAmplitudeGreen(0); // Turn off Green LED
  particleSensor.enableDIETEMPRDY(); //Enable the temp ready interrupt. This is required.

  bufferLength = 100; //buffer length of 100 stores 4 seconds of samples running at 25sps

  //read the first 100 samples, and determine the signal range
  for (byte i = 0 ; i < bufferLength ; i++)
  {
    while (particleSensor.available() == false) //do we have new data?
      particleSensor.check(); //Check the sensor for new data

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample(); //We're finished with this sample so move to next sample
  }

  //calculate heart rate and SpO2 after first 100 samples (first 4 seconds of samples)
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo22, &validSPO2, &heartRate, &validHeartRate);


}

void loop(void)
{
  // ================== HEARTBEAT ==================
  //dumping the first 25 sets of samples in the memory and shift the last 75 sets of samples to the top
  for (byte i = 25; i < 100; i++)
  {
    redBuffer[i - 25] = redBuffer[i];
    irBuffer[i - 25] = irBuffer[i];
  }

  //take 25 sets of samples before calculating the heart rate.
  for (byte i = 75; i < 100; i++)
  {
    while (particleSensor.available() == false) //do we have new data?
      particleSensor.check(); //Check the sensor for new data

    // digitalWrite(readLED, !digitalRead(readLED)); //Blink onboard LED with every data read

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample(); //We're finished with this sample so move to next sample
  }

  //After gathering 25 new samples recalculate HR and SP02
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo22, &validSPO2, &heartRate, &validHeartRate);

  // =============== SPO2 ======================

  // RED TREATMENT
  red_value = particleSensor.getRed();

  // Adjust the indexes for the sampled
  red_sampled[count_red % samples] = red_value;

  // IR TREATMENT
  ir_value = particleSensor.getIR();

  // Adjust the indexes for the sampled
  ir_sampled[count_ir % samples] = ir_value;

  // Reset counters
  if (count_ir >= samples) {
    count_ir = 0;
    count_red = 0;
  }

  // Calculate averages and min/max values
  for (byte i = 0; i < samples; i++){
    red_avg += 1.0 * red_sampled[i];
    ir_avg += 1.0 * ir_sampled[i];

    if (min_red > red_sampled[i]) min_red = red_sampled[i];
    if (max_red < red_sampled[i]) max_red = red_sampled[i];

    if (min_ir > ir_sampled[i]) min_ir = ir_sampled[i];
    if (max_ir < ir_sampled[i]) max_ir = ir_sampled[i];
  }

  // Calculate SpO2
  red_avg /= samples;
  ir_avg /= samples;
  spo2 = 110.0 - 13.0 * ((max_red - min_red) / red_avg) / ((max_ir - min_ir) / ir_avg);
  temperature = particleSensor.readTemperature();

  if(spo2 < 0.0) spo2 = 0.0;
  if(heartRate < 0.0) heartRate = 0.0;
  if(spo2 > 100.0) spo2 = 100.0;
  if(heartRate > 150.0) heartRate = 150.0;

  // Serial.print("SPO2: ");
  // Serial.print(spo2);
  // Serial.print(", BPM=");
  // Serial.println(heartRate);

  // Reset variables
  red_avg = 0;
  max_red = 0;
  min_red = 10000000;
  ir_avg = 0;
  max_ir = 0;
  min_ir = 10000000;

  // Increment counters
  count_red++;
  count_ir++;

  // ==================== WIFI ====================
  server.handleClient();
}