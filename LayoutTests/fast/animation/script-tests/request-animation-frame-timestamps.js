description("Tests the timestamps provided to requestAnimationFrame callbacks");

function busyWait(millis) {
    var start = Date.now();
    while (Date.now()-start < millis) {}
}

var e = document.getElementById("e");
var firstTimestamp = undefined;

window.webkitRequestAnimationFrame(function(timestamp) {
    firstTimestamp = timestamp;
    shouldBeDefined("firstTimestamp");
    busyWait(10);
}, e);

var secondTimestamp = undefined;
window.webkitRequestAnimationFrame(function(timestamp) {
    secondTimestamp = timestamp;
    shouldBeDefined("secondTimestamp");
    shouldBe("firstTimestamp", "secondTimestamp");
}, e);

if (window.layoutTestController)
    layoutTestController.display();

shouldBeDefined("firstTimestamp");

var successfullyParsed = true;
