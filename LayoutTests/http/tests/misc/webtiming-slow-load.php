<!DOCTYPE HTML PUBLIC "-//IETF//DTD HTML//EN">
<html>
<head>
<script>
</script>
<link rel="stylesheet" href="../../js-test-resources/js-test-style.css">
<script src="../../js-test-resources/js-test-pre.js"></script>
</head>
<body>
<p id="description"></p>
<div id="console"></div>
<script>
description("Verifies that requestStart and responseStart are available before the main document has finished loading.");

var performance = window.performance || {};
var navigation = performance.navigation || {};
var timing = performance.timing || {};

shouldBeNonZero("timing.requestStart");
shouldBeNonZero("timing.responseStart");
shouldBe("timing.responseEnd", "0");

var successfullyParsed = true;
</script>
<script src="../../js-test-resources/js-test-post.js"></script>
<?php
    echo str_repeat(" ", 100000);
    flush();
    ob_flush();
    sleep(1);
?>
</body>
</html>
