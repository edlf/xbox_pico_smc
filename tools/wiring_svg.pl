#!/usr/bin/perl
# Generates docs/images/wiring.svg: Pico and PIC footprints with net labels.
# Run from the project root: perl tools/wiring_svg.pl docs/images/wiring.svg
# Keep the nets in step with src/pin_assignments.hpp.
use strict;
use utf8;
use warnings;

my $out = shift or die "usage: wiring_svg.pl out.svg\n";

# Pico physical pins
my @pico = (undef, qw(GP0 GP1 GND GP2 GP3 GP4 GP5 GND GP6 GP7 GP8 GP9 GND GP10 GP11 GP12 GP13 GND GP14 GP15
  GP16 GP17 GND GP18 GP19 GP20 GP21 GND GP22 RUN GP26 GP27 GND GP28 ADC_VREF 3V3 3V3_EN GND VSYS VBUS));
# PIC16LC63A pins
my @pic = (undef, 'MCLR#', 'RA0', 'RA1', 'RA2', 'RA3', 'RA4', 'RA5', 'VSS', 'OSC1', 'OSC2', 'RC0', 'RC1', 'RC2', 'RC3',
  'RC4', 'RC5', 'RC6', 'RC7', 'VSS', 'VDD', 'RB0', 'RB1', 'RB2', 'RB3', 'RB4', 'RB5', 'RB6', 'RB7');

# net => [pico pin, pic pin, type, note]; type: out (Pico drives), in, io, pwr, gnd
my @nets = (
  ['RED_LED',     4,  2, 'out'], ['GREEN_LED',  5,  3, 'out'], ['SYSRESET#',  6,  4, 'out'],
  ['EJECT_SW',    7,  5, 'in'],  ['SMI#',       9,  6, 'out'], ['DVD_EJECT', 10,  7, 'out'],
  ['AUDIO_CLAMP',11, 11, 'out'], ['POWOK',     12, 12, 'in'],  ['FAN_PWM',   14, 13, 'out'],
  ['POWON',      15, 16, 'out'], ['SMB_SDA',   16, 15, 'io'],  ['SMB_SCL',   17, 14, 'io'],
  ['RTC_DUMP',   19, 17, 'out'], ['POWER_SW',  20, 18, 'in'],  ['PLL_EN',    21, 21, 'out'],
  ['AV_MODE0',   22, 22, 'in'],  ['AV_MODE1',  24, 23, 'in'],  ['AV_MODE2',  25, 24, 'in'],
  ['TRAY0',      26, 25, 'in'],  ['TRAY1',     27, 26, 'in'],  ['TRAY2',     29, 27, 'in'],
  ['SMC_RESET#', 30,  1, 'in'],  ['DVD_ACTIVE',31, 28, 'in'],
  ['3V3_STBY',   36, 20, 'pwr'], ['GND',       38,  8, 'gnd'],
);
my %extra_pic = (19 => ['GND', 'gnd']);                  # Second PIC ground
my %extra_pico = (1 => ['UART_TX', 'dbg'], 2 => ['UART_RX', 'dbg']);

my %color = (out => '#c0392b', in => '#2471a3', io => '#7d3c98', pwr => '#d68910', gnd => '#424949',
  dbg => '#1e8449');

my (%pico_net, %pic_net);
for my $n (@nets) {
  my ($name, $pp, $cp, $type) = @$n;
  $pico_net{$pp} = [$name, $type, "PIC $cp"];
  $pic_net{$cp} = [$name, $type, "Pico $pp · $pico[$pp]"];
}
$pic_net{19} = ['GND', 'gnd', 'Pico GND'];
$pico_net{$_} = [$extra_pico{$_}[0], $extra_pico{$_}[1], 'debug only'] for keys %extra_pico;

my $pitch = 25;
my @svg;
sub e { push @svg, @_ }
sub esc { my $s = shift; $s =~ s/&/&amp;/g; $s =~ s/</&lt;/g; $s =~ s/>/&gt;/g; $s }

# A net tag: rounded box with the net name and where it goes
sub tag {
  my ($x, $y, $align, $net) = @_;   # align: 'left' = tag ends at x, 'right' = tag starts at x
  my ($name, $type, $to) = @$net;
  my $w = 190;
  my $x0 = $align eq 'left' ? $x - $w : $x;
  my $c = $color{$type};
  e(sprintf('<rect x="%d" y="%d" width="%d" height="20" rx="4" fill="%s" fill-opacity="0.12" stroke="%s"/>',
    $x0, $y - 10, $w, $c, $c));
  e(sprintf('<text x="%d" y="%d" class="net" fill="%s">%s</text>', $x0 + 8, $y + 4, $c, esc($name)));
  e(sprintf('<text x="%d" y="%d" class="to" text-anchor="end">%s</text>', $x0 + $w - 7, $y + 4, esc($to)));
}

# A part: body with pins down the left (top to bottom) and up the right
sub part {
  my ($x, $y, $w, $title, $sub, $names, $nets, $count) = @_;
  my $half = $count / 2;
  my $h = $half * $pitch + 20;
  e(sprintf('<rect x="%d" y="%d" width="%d" height="%d" rx="8" class="body"/>', $x, $y, $w, $h));
  e(sprintf('<path d="M %d %d a 12 12 0 0 0 24 0" class="notch"/>', $x + $w / 2 - 12, $y));
  e(sprintf('<text x="%d" y="%d" class="title" text-anchor="middle">%s</text>', $x + $w / 2, $y + $h / 2 - 6, esc($title)));
  e(sprintf('<text x="%d" y="%d" class="sub" text-anchor="middle">%s</text>', $x + $w / 2, $y + $h / 2 + 14, esc($sub)));
  for my $i (1 .. $count) {
    my $left = $i <= $half;
    my $row = $left ? $i - 1 : $count - $i;
    my $py = $y + 20 + $row * $pitch;
    my $px = $left ? $x : $x + $w;
    my $used = exists $nets->{$i};
    my $c = $used ? $color{$nets->{$i}[1]} : '#b3b6b7';
    my $stub = $used ? 40 : 14;
    my $x2 = $left ? $px - $stub : $px + $stub;
    e(sprintf('<line x1="%d" y1="%d" x2="%d" y2="%d" stroke="%s" stroke-width="%s"/>', $px, $py, $x2, $py, $c, $used ? 2 : 1));
    e(sprintf('<circle cx="%d" cy="%d" r="2.5" fill="%s"/>', $x2, $py, $c)) if $used;
    # Pin number outside the body, name inside
    e(sprintf('<text x="%d" y="%d" class="num" text-anchor="%s">%d</text>', $left ? $px - 4 : $px + 4, $py - 4,
      $left ? 'end' : 'start', $i));
    e(sprintf('<text x="%d" y="%d" class="pin%s" text-anchor="%s">%s</text>', $left ? $px + 8 : $px - 8, $py + 4,
      $used ? '' : ' unused', $left ? 'start' : 'end', esc($names->[$i])));
    tag($left ? $x2 - 4 : $x2 + 4, $py, $left ? 'left' : 'right', $nets->{$i}) if $used;
  }
}

my ($W, $H) = (1620, 650);
e(qq{<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 $W $H" width="$W" height="$H" font-family="Segoe UI, Helvetica, Arial, sans-serif">});
e('<style>.body{fill:#f4f6f6;stroke:#2c3e50;stroke-width:2}.notch{fill:none;stroke:#2c3e50}',
  '.title{font-size:18px;font-weight:600;fill:#2c3e50}.sub{font-size:12px;fill:#566573}',
  '.num{font-size:9px;fill:#7f8c8d}.pin{font-size:11px;fill:#2c3e50;font-family:Consolas,Menlo,monospace}',
  '.unused{fill:#b3b6b7}.net{font-size:11px;font-weight:600;font-family:Consolas,Menlo,monospace}',
  '.to{font-size:10px;fill:#566573}.h{font-size:20px;font-weight:600;fill:#2c3e50}.leg{font-size:12px;fill:#2c3e50}</style>');
e(qq{<rect width="$W" height="$H" fill="#ffffff"/>});
e('<text x="24" y="34" class="h">OG SMC wiring: Raspberry Pi Pico to the Xbox PIC footprint (U7)</text>');
e('<text x="24" y="54" class="sub">Same net name = connect together. Top view, notch at the top, pin 1 top left. Pico, Pico W, Pico 2 and Pico 2 W share this pinout.</text>');

part(290, 90, 280, 'Raspberry Pi Pico', 'top view', \@pico, \%pico_net, 40);
part(1100, 90, 260, 'PIC16LC63A', 'U7 footprint, top view', \@pic, \%pic_net, 28);

# Legend
my @leg = (['out', 'Pico output'], ['in', 'Pico input'], ['io', 'SMBus (I2C, both ways)'], ['pwr', '3.3 V standby'],
  ['gnd', 'Ground'], ['dbg', 'Debug UART only']);
my $lx = 880;
my $ly = 515;
e(sprintf('<text x="%d" y="%d" class="leg" font-weight="600">Legend</text>', $lx, $ly - 14));
for my $i (0 .. $#leg) {
  my ($t, $label) = @{$leg[$i]};
  my $yy = $ly + $i * 20;
  e(sprintf('<rect x="%d" y="%d" width="26" height="14" rx="3" fill="%s" fill-opacity="0.12" stroke="%s"/>',
    $lx, $yy - 10, $color{$t}, $color{$t}));
  e(sprintf('<text x="%d" y="%d" class="leg">%s</text>', $lx + 36, $yy + 1, esc($label)));
}
e('</svg>');

open my $fh, '>:encoding(UTF-8)', $out or die "$out: $!";
print $fh join("\n", @svg), "\n";
close $fh;
