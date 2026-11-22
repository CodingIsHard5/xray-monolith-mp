BEGIN{FS="\t"}
{sub(/\r$/,"")}
$1=="sq"{sl[$2]=$4; sg[$2]=$5; ss[$2]=$6; sn[$2]=$7; nsq++}
$1=="cr"{ if($8!="-"){ c[$8]++; crl[$2]=$6; crg[$2]=$7; crs[$2]=$8; crm[$2]=$9; ncr++ } }
$1=="sm"{ smn[$2]=$5 }
END{
 for(i in crs){ s=crs[i]; if(!(s in sl)) i1++; else { if(crl[i]!=sl[s]) i2++; if(crg[i]!=sg[s]) i3++; if(crm[i]!=ss[s]) i5++ } }
 for(s in sn){ if((c[s]+0)!=sn[s]) i4++; if(ss[s]!="-") sum[ss[s]]+=sn[s] }
 for(m in smn){ if((sum[m]+0)!=smn[m]) i6++ }
 printf "sq=%d cr_in_sq=%d | I1 orphan=%d I2 level=%d I3 gv=%d I4 count=%d I5 smart=%d I6 smart_pop=%d\n", nsq, ncr, i1, i2, i3, i4, i5, i6
}
