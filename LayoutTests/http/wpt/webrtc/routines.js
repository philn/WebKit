// Test inspired from https://webrtc.github.io/samples/
var localConnection;
var remoteConnection;

function createConnections(setupLocalConnection, setupRemoteConnection, options = { }) {
    localConnection = new RTCPeerConnection();
    remoteConnection = new RTCPeerConnection();
    remoteConnection.onicecandidate = (event) => { iceCallback2(event, options.filterOutICECandidate) };

    localConnection.onicecandidate = (event) => { iceCallback1(event, options.filterOutICECandidate) };

    Promise.resolve(setupLocalConnection(localConnection)).then(() => {
        return Promise.resolve(setupRemoteConnection(remoteConnection));
    }).then(() => {
        localConnection.createOffer().then((desc) => gotDescription1(desc, options), onCreateSessionDescriptionError);
    });

    return [localConnection, remoteConnection]
}

function closeConnections()
{
    localConnection.close();
    remoteConnection.close();
}

function onCreateSessionDescriptionError(error)
{
    assert_unreached();
}

function gotDescription1(desc, options)
{
    if (options.observeOffer) {
        const result = options.observeOffer(desc);
        if (result)
            desc = result;
    }

    localConnection.setLocalDescription(desc);
    remoteConnection.setRemoteDescription(desc).then(() => {
        remoteConnection.createAnswer().then((desc) => gotDescription2(desc, options), onCreateSessionDescriptionError);
    });
}

function gotDescription2(desc, options)
{
    if (options.observeAnswer)
        options.observeAnswer(desc);

    remoteConnection.setLocalDescription(desc);
    localConnection.setRemoteDescription(desc);
}

function iceCallback1(event, filterOutICECandidate)
{
    if (filterOutICECandidate && filterOutICECandidate(event.candidate))
        return;

    remoteConnection.addIceCandidate(event.candidate).then(onAddIceCandidateSuccess, onAddIceCandidateError);
}

function iceCallback2(event, filterOutICECandidate)
{
    if (filterOutICECandidate && filterOutICECandidate(event.candidate))
        return;

    localConnection.addIceCandidate(event.candidate).then(onAddIceCandidateSuccess, onAddIceCandidateError);
}

function onAddIceCandidateSuccess()
{
}

function onAddIceCandidateError(error)
{
    console.log("addIceCandidate error: " + error)
    assert_unreached();
}

async function renegotiate(pc1, pc2)
{
    let d = await pc1.createOffer();
    await pc1.setLocalDescription(d);
    await pc2.setRemoteDescription(d);
    d = await pc2.createAnswer();
    await pc1.setRemoteDescription(d);
    await pc2.setLocalDescription(d);
}

function analyseAudio(stream, duration, context)
{
    return new Promise((resolve, reject) => {
        var sourceNode = context.createMediaStreamSource(stream);

        var analyser = context.createAnalyser();
        var gain = context.createGain();

        var results = { heardHum: false, heardBip: false, heardBop: false, heardNoise: false };

        analyser.fftSize = 2048;
        analyser.smoothingTimeConstant = 0;
        analyser.minDecibels = -100;
        analyser.maxDecibels = 0;
        gain.gain.value = 0;

        sourceNode.connect(analyser);
        analyser.connect(gain);
        gain.connect(context.destination);

       function analyse() {
           var freqDomain = new Uint8Array(analyser.frequencyBinCount);
           analyser.getByteFrequencyData(freqDomain);

           var hasFrequency = expectedFrequency => {
                var bin = Math.floor(expectedFrequency * analyser.fftSize / context.sampleRate);
                return bin < freqDomain.length && freqDomain[bin] >= 100;
           };

           if (!results.heardHum)
                results.heardHum = hasFrequency(150);

           if (!results.heardBip)
               results.heardBip = hasFrequency(1500);

           if (!results.heardBop)
                results.heardBop = hasFrequency(500);

           if (!results.heardNoise)
                results.heardNoise = hasFrequency(3000);

           if (results.heardHum && results.heardBip && results.heardBop && results.heardNoise)
                done();
        };

       function done() {
            clearTimeout(timeout);
            clearInterval(interval);
            resolve(results);
       }

        var timeout = setTimeout(done, 3 * duration);
        var interval = setInterval(analyse, duration / 30);
        analyse();
    });
}

function waitFor(duration)
{
    return new Promise((resolve) => setTimeout(resolve, duration));
}

function waitForVideoSize(video, width, height, count)
{
    if (video.videoWidth === width && video.videoHeight === height)
        return Promise.resolve("video has expected size");

    if (count === undefined)
        count = 0;
    if (++count > 20)
        return Promise.reject("waitForVideoSize timed out, expected " + width + "x"+ height + " but got " + video.videoWidth + "x" + video.videoHeight);

    return waitFor(100).then(() => {
        return waitForVideoSize(video, width, height, count);
    });
}

async function doHumAnalysis(stream, expected)
{
    var context = new AudioContext();
    for (var cptr = 0; cptr < 20; cptr++) {
        var results = await analyseAudio(stream, 200, context);
        if (results.heardHum === expected)
            return true;
        await waitFor(50);
    }
    await context.close();
    return false;
}

function isVideoBlack(canvas, video, startX, startY, grabbedWidth, grabbedHeight)
{
    canvas.width = video.videoWidth;
    canvas.height = video.videoHeight;
    if (!grabbedHeight) {
        startX = 10;
        startY = 10;
        grabbedWidth = canvas.width - 20;
        grabbedHeight = canvas.height - 20;
    }

    canvas.getContext('2d').drawImage(video, 0, 0, canvas.width, canvas.height);

    imageData = canvas.getContext('2d').getImageData(startX, startY, grabbedWidth, grabbedHeight);
    data = imageData.data;
    for (var cptr = 0; cptr < grabbedWidth * grabbedHeight; ++cptr) {
        // Approximatively black pixels.
        if (data[4 * cptr] > 30 || data[4 * cptr + 1] > 30 || data[4 * cptr + 2] > 30)
            return false;
    }
    return true;
}

async function checkVideoBlack(expected, canvas, video, errorMessage, counter)
{
    if (isVideoBlack(canvas, video) === expected)
        return Promise.resolve();

    if (counter === undefined)
        counter = 0;
    if (counter > 400) {
        if (!errorMessage)
            errorMessage = "checkVideoBlack timed out expecting " + expected;
        return Promise.reject(errorMessage);
    }

    await waitFor(50);
    return checkVideoBlack(expected, canvas, video, errorMessage, ++counter);
}

function setCodec(sdp, codec)
{
    return sdp.split('\r\n').filter(line => {
        return line.indexOf('a=fmtp') === -1 && line.indexOf('a=rtcp-fb') === -1 && (line.indexOf('a=rtpmap') === -1 || line.indexOf(codec) !== -1);
    }).join('\r\n');
}

async function getTypedStats(connection, type)
{
    const report = await connection.getStats();
    var stats;
    report.forEach((statItem) => {
        if (statItem.type === type)
            stats = statItem;
    });
    return stats;
}

function getReceivedTrackStats(connection)
{
    return connection.getStats().then((report) => {
        var stats;
        report.forEach((statItem) => {
            if (statItem.type === "track") {
                stats = statItem;
            }
        });
        return stats;
    });
}

async function computeFrameRate(stream, video)
{
    if (window.internals) {
        internals.observeMediaStreamTrack(stream.getVideoTracks()[0]);
        await new Promise(resolve => setTimeout(resolve, 1000)); 
        return internals.trackVideoSampleCount;
    }

    let connection;
    video.srcObject = await new Promise((resolve, reject) => {
        createConnections((firstConnection) => {
            firstConnection.addTrack(stream.getVideoTracks()[0], stream);
        }, (secondConnection) => {
            connection = secondConnection;
            secondConnection.ontrack = (trackEvent) => {
                resolve(trackEvent.streams[0]);
            };
        });
        setTimeout(() => reject("Test timed out"), 5000);
    });

    await video.play();

    const stats1 = await getReceivedTrackStats(connection);
    await new Promise(resolve => setTimeout(resolve, 1000)); 
    const stats2 = await getReceivedTrackStats(connection);
    return (stats2.framesReceived - stats1.framesReceived) * 1000 / (stats2.timestamp - stats1.timestamp);
}

function setH264BaselineCodec(sdp)
{
    const lines = sdp.split('\r\n');
    const h264Lines = lines.filter(line => line.indexOf("a=fmtp") === 0 && line.indexOf("42e01f") !== -1);
    const baselineNumber = h264Lines[0].substring(6).split(' ')[0];
    return lines.filter(line => {
        return (line.indexOf('a=fmtp') === -1 && line.indexOf('a=rtcp-fb') === -1 && line.indexOf('a=rtpmap') === -1) || line.indexOf(baselineNumber) !== -1;
    }).join('\r\n');
}

function setH264HighCodec(sdp)
{
    const lines = sdp.split('\r\n');
    const h264Lines = lines.filter(line => line.indexOf("a=fmtp") === 0 && line.indexOf("640c1f") !== -1);
    const baselineNumber = h264Lines[0].substring(6).split(' ')[0];
    return lines.filter(line => {
        return (line.indexOf('a=fmtp') === -1 && line.indexOf('a=rtcp-fb') === -1 && line.indexOf('a=rtpmap') === -1) || line.indexOf(baselineNumber) !== -1;
    }).join('\r\n');
}

const audioLineRegex = /\r\nm=audio.+\r\n/g;
const videoLineRegex = /\r\nm=video.+\r\n/g;
const applicationLineRegex = /\r\nm=application.+\r\n/g;

function countLine(sdp, regex) {
    const matches = sdp.match(regex);
    if(matches === null) {
        return 0;
    } else {
        return matches.length;
    }
}

function countAudioLine(sdp) {
    return countLine(sdp, audioLineRegex);
}

function countVideoLine(sdp) {
    return countLine(sdp, videoLineRegex);
}

function countApplicationLine(sdp) {
    return countLine(sdp, applicationLineRegex);
}


// These media tracks will be continually updated with deterministic "noise" in
// order to ensure UAs do not cease transmission in response to apparent
// silence.
//
// > Many codecs and systems are capable of detecting "silence" and changing
// > their behavior in this case by doing things such as not transmitting any
// > media.
//
// Source: https://w3c.github.io/webrtc-pc/#offer-answer-options
const trackFactories = {
  // Share a single context between tests to avoid exceeding resource limits
  // without requiring explicit destruction.
  audioContext: null,

  /**
   * Given a set of requested media types, determine if the user agent is
   * capable of procedurally generating a suitable media stream.
   *
   * @param {object} requested
   * @param {boolean} [requested.audio] - flag indicating whether the desired
   *                                      stream should include an audio track
   * @param {boolean} [requested.video] - flag indicating whether the desired
   *                                      stream should include a video track
   *
   * @returns {boolean}
   */
  canCreate(requested) {
    const supported = {
      audio: !!window.AudioContext && !!window.MediaStreamAudioDestinationNode,
      video: !!HTMLCanvasElement.prototype.captureStream
    };

    return (!requested.audio || supported.audio) &&
      (!requested.video || supported.video);
  },

  audio() {
    const ctx = trackFactories.audioContext = trackFactories.audioContext ||
      new AudioContext();
    const oscillator = ctx.createOscillator();
    const dst = oscillator.connect(ctx.createMediaStreamDestination());
    oscillator.start();
    return dst.stream.getAudioTracks()[0];
  },

  video({width = 640, height = 480, signal} = {}) {
    const canvas = Object.assign(
      document.createElement("canvas"), {width, height}
    );
    const ctx = canvas.getContext('2d');
    const stream = canvas.captureStream();

    let count = 0;
    const interval = setInterval(() => {
      ctx.fillStyle = `rgb(${count%255}, ${count*count%255}, ${count%255})`;
      count += 1;
      ctx.fillRect(0, 0, width, height);
      // Add some bouncing boxes in contrast color to add a little more noise.
      const contrast = count + 128;
      ctx.fillStyle = `rgb(${contrast%255}, ${contrast*contrast%255}, ${contrast%255})`;
      const xpos = count % (width - 20);
      const ypos = count % (height - 20);
      ctx.fillRect(xpos, ypos, xpos + 20, ypos + 20);
      const xpos2 = (count + width / 2) % (width - 20);
      const ypos2 = (count + height / 2) % (height - 20);
      ctx.fillRect(xpos2, ypos2, xpos2 + 20, ypos2 + 20);
      // If signal is set (0-255), add a constant-color box of that luminance to
      // the video frame at coordinates 20 to 60 in both X and Y direction.
      // (big enough to avoid color bleed from surrounding video in some codecs,
      // for more stable tests).
      if (signal != undefined) {
        ctx.fillStyle = `rgb(${signal}, ${signal}, ${signal})`;
        ctx.fillRect(20, 20, 40, 40);
      }
    }, 100);

    if (document.body) {
      document.body.appendChild(canvas);
    } else {
      document.addEventListener('DOMContentLoaded', () => {
        document.body.appendChild(canvas);
      }, {once: true});
    }

    // Implement track.stop() for performance in some tests on some platforms
    const track = stream.getVideoTracks()[0];
    const nativeStop = track.stop;
    track.stop = function stop() {
      clearInterval(interval);
      nativeStop.apply(this);
      if (document.body && canvas.parentElement == document.body) {
        document.body.removeChild(canvas);
      }
    };
    return track;
  }
};


// Generate a MediaStream bearing the specified tracks.
//
// @param {object} [caps]
// @param {boolean} [caps.audio] - flag indicating whether the generated stream
//                                 should include an audio track
// @param {boolean} [caps.video] - flag indicating whether the generated stream
//                                 should include a video track, or parameters for video
async function getNoiseStream(caps = {}) {
  if (!trackFactories.canCreate(caps)) {
    return navigator.mediaDevices.getUserMedia(caps);
  }
  const tracks = [];

  if (caps.audio) {
    tracks.push(trackFactories.audio());
  }

  if (caps.video) {
    tracks.push(trackFactories.video(caps.video));
  }

  return new MediaStream(tracks);
}

// Obtain a MediaStreamTrack of kind using procedurally-generated streams (and
// falling back to `getUserMedia` when the user agent cannot generate the
// requested streams).
// Return Promise of pair of track and associated mediaStream.
// Assumes that there is at least one available device
// to generate the track.
function getTrackFromUserMedia(kind) {
  return getNoiseStream({ [kind]: true })
  .then(mediaStream => {
    const [track] = mediaStream.getTracks();
    return [track, mediaStream];
  });
}
