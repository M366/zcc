def check(n)
  s = "*"*n
  f = open("test.c","w")
  f.puts <<EOS
int printf();
int main(){
(#{s}printf)("OK\\n");
}
EOS
  f.close()
  return system("./zcc test.c 1> tmp.s 2> /dev/null")
end

def binary_search
  s = 1
  e = 100000
  while s!=e and s+1!=e
    m = (s+e)/2
    if check(m)
      puts "#{m} OK"
      s = m
    else
      puts "#{m} NG"
      e = m
    end
  end
end

binary_search
