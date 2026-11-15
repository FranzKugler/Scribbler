/**
 * signal
 * Signal strength as 1..4 bars.
 *
 * Its own file because two places need the same answer and must not disagree:
 * the network list draws bars with it, and the WLAN tab's connection card puts
 * the matching word next to the raw dBm. One of them showing three bars while
 * the other says "fair" would be a small thing that makes the whole page look
 * unreliable.
 *
 * The thresholds are the usual ones: -55 and better is as good as it gets
 * indoors, -75 and worse is where a connection starts dropping.
 *
 * @author   Franz Kugler / franz _AT_ franz _MINUS_ kugler _DOT_ de
 * @version  1.0
 * @created  31.8.2026
 * @updated  31.8.2026
 */
export function bars(rssi) {
  if (rssi >= -55) return 4;
  if (rssi >= -67) return 3;
  if (rssi >= -75) return 2;
  return 1;
}
