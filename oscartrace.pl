# Perl script to interpret OS/2 IPTRACE formatted data and list a sequence
# of FLAPs/SNACs for the Oscar protocol in it.

if ( $#ARGV == -1 )
{
  die "Please specify the filename of an IP dump from IPFORMAT.\n";
} elsif ( $#ARGV > 0 )
{
  die "Only one parameter is accepted by this script (an IPFORMAT dump file).\n";
}

open (TRACEFILE, "<$ARGV[0]") || die "Couldn't read file: $ARGV[0]\n";

open (IFCONFIG, "ifconfig lan0 |") || die "Couldn't run ifconfig.\n";
while (<IFCONFIG>)
{
  if ( /inet\s+(\d+)\.(\d+)\.(\d+)\.(\d+)/ )
  {
    $myHostID = "$1\.$2\.$3\.$4";
  }
}
close IFCONFIG;

while (<TRACEFILE>)
{
  if ( /IP:\s+Dest:\s+(\d+)\.(\d+)\.(\d+)\.(\d+)\s+Source:\s+(\d+)\.(\d+)\.(\d+)\.(\d+)/ )
  {
    $w = int $1;
    $x = int $2;
    $y = int $3;
    $z = int $4;
    $a = int $5;
    $b = int $6;
    $c = int $7;
    $d = int $8;
    $packetDest = "$w.$x.$y.$z";
    $packetSource = "$a.$b.$c.$d";
    while (<TRACEFILE>)
    {
      if ( /^\s*$/ )
      {
        # blank line separates packets
        last;
      } elsif ( /TCP:\s+Source Port:\s+(\d+).+Dest Port:\s+(\d+)/ )
      {
        $packetDestPort = $2;
        $packetSourcePort = $1;
      } elsif ( /[-]+\s+DATA\s+[-]+/ )
      {
        # data dump follows
        if ( ($packetSource != $myHostID && $packetDest != $myHostID) ||
              ($packetSourcePort != 5190 && $packetDestPort != 5190) )
        {
          # Either a broadcast message or one not involving OSCAR
          last;
        }
        
        $currentSpot = 1;
        $_ = <TRACEFILE>;
        @data = split;
        
        if ( $packetSource == $myHostID )
        {
          print "Data sent to OSCAR/BOS server ($packetDest):\n";
        } else {
          print "Data received from OSCAR/BOS server ($packetSource):\n";
        }
        
        while ( !/^\s*$/ )
        {
          if ( $data[$currentSpot] != "2A" )
          {
            if ( $currentSpot != $#data )
            {
              die "Bad FLAP detected! - start byte is $data[$currentSpot] instead of 0x2A.\n";
            } else {
              last;
            }
          } else {
            ++$currentSpot;
            if ( $currentSpot > 16 )
            {
              $_ = <TRACEFILE>;
              @data = split;
              $currentSpot = 1;
            }
            $FLAPchannel = $data[$currentSpot];
            print "FLAP channel:     0x$FLAPchannel\n";
            $currentSpot++;
            if ( $currentSpot > 16 )
            {
              $_ = <TRACEFILE>;
              @data = split;
              $currentSpot -= 16;
            }

            $FLAPseqHigh = $data[$currentSpot];
            $currentSpot++;
            if ( $currentSpot > 16 )
            {
              $_ = <TRACEFILE>;
              @data = split;
              $currentSpot = 1;
            }
            $FLAPseqLow = $data[$currentSpot];
            $currentSpot++;
            if ( $currentSpot > 16 )
            {
              $_ = <TRACEFILE>;
              @data = split;
              $currentSpot = 1;
            }
            $FLAPsequence = "$FLAPseqHigh"."$FLAPseqLow";
            print "FLAP sequence:    0x$FLAPsequence\n";

            $FLAPlenHigh = $data[$currentSpot];
            $currentSpot++;
            if ( $currentSpot > 16 )
            {
              $_ = <TRACEFILE>;
              @data = split;
              $currentSpot = 1;
            }
            $FLAPlenLow = $data[$currentSpot];
            $currentSpot++;
            if ( $currentSpot > 16 )
            {
              $_ = <TRACEFILE>;
              @data = split;
              $currentSpot = 1;
            }
            $FLAPlength = hex( "$FLAPlenHigh"."$FLAPlenLow" );
            
            print "FLAP data length: $FLAPlength\n";
            
            if ( $FLAPchannel == 2 )
            {
              # SNAC data to interpret
              $SNACfamHigh = $data[$currentSpot];
              $currentSpot++;
              if ( $currentSpot > 16 )
              {
                $_ = <TRACEFILE>;
                @data = split;
                $currentSpot = 1;
              }
              $SNACfamLow = $data[$currentSpot];
              $currentSpot++;
              if ( $currentSpot > 16 )
              {
                $_ = <TRACEFILE>;
                @data = split;
                $currentSpot = 1;
              }
              $SNACfamily = "$SNACfamHigh"."$SNACfamLow";
              print "SNAC family:  0x$SNACfamily\n";
              
              $SNACsubHigh = $data[$currentSpot];
              $currentSpot++;
              if ( $currentSpot > 16 )
              {
                $_ = <TRACEFILE>;
                @data = split;
                $currentSpot = 1;
              }
              $SNACsubLow = $data[$currentSpot];
              $currentSpot++;
              if ( $currentSpot > 16 )
              {
                $_ = <TRACEFILE>;
                @data = split;
                $currentSpot = 1;
              }
              $SNACsubType = "$SNACsubHigh"."$SNACsubLow";
              print "SNAC subtype: 0x$SNACsubType\n";
              
              $SNACflagsHigh = $data[$currentSpot];
              $currentSpot++;
              if ( $currentSpot > 16 )
              {
                $_ = <TRACEFILE>;
                @data = split;
                $currentSpot = 1;
              }
              $SNACflagsLow = $data[$currentSpot];
              $currentSpot++;
              if ( $currentSpot > 16 )
              {
                $_ = <TRACEFILE>;
                @data = split;
                $currentSpot = 1;
              }
              $SNACflags = "$SNACflagsHigh"."$SNACflagsLow";
              print "SNAC flags:   0x$SNACflags\n";
              
              $SNACreq3 = $data[$currentSpot];
              $currentSpot++;
              if ( $currentSpot > 16 )
              {
                $_ = <TRACEFILE>;
                @data = split;
                $currentSpot = 1;
              }
              $SNACreq2 = $data[$currentSpot];
              $currentSpot++;
              if ( $currentSpot > 16 )
              {
                $_ = <TRACEFILE>;
                @data = split;
                $currentSpot = 1;
              }
              $SNACreq1 = $data[$currentSpot];
              $currentSpot++;
              if ( $currentSpot > 16 )
              {
                $_ = <TRACEFILE>;
                @data = split;
                $currentSpot = 1;
              }
              $SNACreq0 = $data[$currentSpot];
              $currentSpot++;
              if ( $currentSpot > 16 )
              {
                $_ = <TRACEFILE>;
                @data = split;
                $currentSpot = 1;
              }
              $SNACrequestID = "$SNACreq3"."$SNACreq2"."$SNACreq1"."$SNACreq0";
              print "SNAC request: 0x$SNACrequestID\n";
              $rawDataStart = 10;
            } else {
              $rawDataStart = 0;
            }
            
            $dataPos = 0;
            $asciiText = "";
            print "Raw data follows:\n";
            for ( $i=$rawDataStart; $i<$FLAPlength; ++$i )
            {
              print "$data[$currentSpot] ";
              ++$dataPos;
              $asciiVal = hex( $data[$currentSpot] );
              
              if ( $asciiVal >= 32 && $asciiVal <= 126 )
              {
                $asciiText .= chr( $asciiVal );
              } else {
                $asciiText .= ".";
              }
              
              if ( $dataPos == 16 )
              {
                $dataPos = 0;
                print "   ".$asciiText."\n";
                $asciiText = "";
              } elsif ( $dataPos == 8 )
              {
                print "   ";
              }
              $currentSpot++;
              if ( $currentSpot > 16 )
              {
                $_ = <TRACEFILE>;
                @data = split;
                $currentSpot = 1;
              }
            }
            if ( $dataPos )
            {
              if ( $dataPos < 8 )
              {
                print "   ";
              }
              for ( $i=$dataPos; $i<16; $i++ )
              {
                print "   ";
              }
              print "   $asciiText";
            }
            print "\n\n";
          }
        }
      }
    }
  }
}

