# Run interactively in Barnix; useradd/su/sudo prompt for passwords.
whoami
useradd alice
write /home/alice/report hello
chmod alice=r passwd=unlock file=/home/alice/report
su alice
cat report
# Must fail:
write report blocked
write /etc/userpasswd.cfg blocked
# Correct ACL password allows changing rights:
chmod alice=rw passwd=unlock file=report
write report allowed
exit
whoami
