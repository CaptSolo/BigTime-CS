// Weather companion: fetches current conditions from Open-Meteo (no API key).

var WMO_LABELS = [
  [0, 'SUNNY'],
  [1, 'CLEAR'],
  [2, 'PT CLOUDY'],
  [3, 'CLOUDY'],
  [48, 'FOG'],
  [55, 'DRIZZLE'],
  [57, 'FRZ DRIZZLE'],
  [65, 'RAIN'],
  [67, 'FRZ RAIN'],
  [77, 'SNOW'],
  [82, 'SHOWERS'],
  [86, 'SNOW'],
  [99, 'STORM']
];

function labelForCode(code) {
  for (var i = 0; i < WMO_LABELS.length; i++) {
    if (code <= WMO_LABELS[i][0]) {
      return WMO_LABELS[i][1];
    }
  }
  return 'WEATHER';
}

function sendWeather(temperature, conditions) {
  Pebble.sendAppMessage(
    { TEMPERATURE: temperature, CONDITIONS: conditions },
    function () {},
    function (e) { console.log('Weather send failed: ' + JSON.stringify(e)); }
  );
}

function fetchWeather(pos) {
  var url = 'https://api.open-meteo.com/v1/forecast' +
    '?latitude=' + pos.coords.latitude +
    '&longitude=' + pos.coords.longitude +
    '&current=temperature_2m,weather_code';

  var xhr = new XMLHttpRequest();
  xhr.onload = function () {
    try {
      var json = JSON.parse(this.responseText);
      var temp = Math.round(json.current.temperature_2m);
      var conditions = labelForCode(json.current.weather_code);
      sendWeather(temp, conditions);
    } catch (err) {
      console.log('Weather parse failed: ' + err);
    }
  };
  xhr.onerror = function () { console.log('Weather request failed'); };
  xhr.open('GET', url);
  xhr.send();
}

function updateWeather() {
  navigator.geolocation.getCurrentPosition(
    fetchWeather,
    function (err) { console.log('Location error: ' + JSON.stringify(err)); },
    { timeout: 15000, maximumAge: 60 * 60 * 1000 }
  );
}

Pebble.addEventListener('ready', updateWeather);
Pebble.addEventListener('appmessage', updateWeather);
