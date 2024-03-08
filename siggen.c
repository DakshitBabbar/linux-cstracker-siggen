#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/list.h>
#include <linux/slab.h> 
#include <linux/timer.h>
#include <linux/sched/signal.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("On Demand Signal Generator Module");
MODULE_AUTHOR("Dakshit Babbar");

#define PROCFS_NAME "sig_target"

//HZ = number of clock ticks in one second
//1 jiffy = HZ number of ticks = 1/HZ s = 1000/HZ ms
//hence here we have defined the timer_interval to be equal to the number of ticks in one second
//as we will be adding this to the "jiffies" macro which equals the number of ticks up until now
#define TIMER_INTERVAL (30 * HZ)

//struct to keep the data of the pending signal
//along with the list head to hold these as nodes
struct pending_signal{
	pid_t pid;
	int signal;
	struct list_head list_node;
};

//the doubly linked list head that is used to store the signals that the processes want to send
static LIST_HEAD(pending_signal_list);

//define a lock for securing the list
static DEFINE_SPINLOCK(list_lock);

// //this will be called whenever a user function will read the proc file
// static ssize_t proc_read_callback(struct file *file, char __user *buffer, size_t data_size, loff_t *pos){
// 	return 0;
// }

//this will be called whenever a user function will write to the proc file 
static ssize_t proc_write_callback(struct file *file, const char __user *buffer, size_t data_size, loff_t *pos){
	//copy data from user buffer to kernel buffer'
	pr_info("---write callback START---");
	pr_info("TRYING TO FILL KERNEL BUFFER FROM USER SPACE");
	// char *kbuffer=(char*)kmalloc(sizeof(char)*data_size, GFP_KERNEL);
	char kbuffer[data_size];
	if(copy_from_user(kbuffer, buffer, data_size)){
		pr_info("FAILED TO COPY DATA FROM USER");
        return -12;
	}

	//make a new node
	pr_info("TRYING TO MAKE NEW NODE");
	struct pending_signal *new_node=kmalloc(sizeof(struct pending_signal), GFP_KERNEL);
	if (!new_node){
        pr_info("FAILED TO ALLOCATE MEMORY FOR A NEW PID_NODE");
        return -12;
    } 

	//parse the value of pid and signal number
	pr_info("PARSING THE INPUT PID AND SIGNUM");
	char *endptr;
	char *endptr2;
	new_node->pid=simple_strtol(kbuffer, &endptr, 10);
	pr_info("PARSED PID=%d", new_node->pid);
	endptr+=2;
	new_node->signal=simple_strtol(endptr, &endptr2, 10);
	pr_info("PARSED SIGNUM=%d", new_node->signal);
	INIT_LIST_HEAD(&new_node->list_node);

	//add the new node to the list
	pr_info("ADDING NODE TO LIST");
	spin_lock(&list_lock);
	list_add_tail(&new_node->list_node, &pending_signal_list);
	spin_unlock(&list_lock);

	// //free the buffer space
	// kfree(kbuffer);
	pr_info("---write callback END---");
	return data_size;
}

//proc_ops struct, used to specify which all callback functions should the proc file system use
static struct proc_ops pops={
	//.proc_read = proc_read_callback,
	.proc_write = proc_write_callback,
};

//this will store the info about out procfs entry
static struct proc_dir_entry* pending_signal_proc_entry;

//make a timer instance for our function
static struct timer_list pending_signal_timer;

//local helper function that sends the signals from the list and deletes the nodes on the go
static void send_signals(void){
	struct pending_signal *entry, *temp;
	pid_t pid;
	int signal;
	struct task_struct *task;

	//iterate through the list and process the signals
	spin_lock(&list_lock);
	list_for_each_entry_safe(entry, temp, &pending_signal_list, list_node) {
		pr_info("ITERATING");
		pid=entry->pid;
		signal=entry->signal;
		task = pid_task(find_vpid(pid), PIDTYPE_PID);
		if(!task){
			pr_info("FAILED TO GET THE TASK STRUCT FOR THE PID %d", pid);
			//delete the node
	        list_del(&entry->list_node);
	        kfree(entry); 
			continue;
		}

		//send signal
		if (send_sig(signal, task, 0) < 0) {
			pr_info("FAILED TO SEND SIGNAL %d TO THE TASK WITH PID %d", signal, pid);
			//delete the node
	        list_del(&entry->list_node);
	        kfree(entry);
	        continue;
	    }

	    //delete the node
        list_del(&entry->list_node);
        kfree(entry);   
    }
    spin_unlock(&list_lock);
}

//this function will be called everytime the timer expires
static void pending_signal_timer_callback(struct timer_list *t){
	pr_info("---timer callback START---");

	//send the signals and empty out the list
	send_signals();

    //restart the timer
	mod_timer(&pending_signal_timer, jiffies + TIMER_INTERVAL);
	pr_info("---timer callback END---");
	return;
}


int init_module(){
	//make the procfs entry
	pending_signal_proc_entry = proc_create(PROCFS_NAME, 0, NULL, &pops);
	if (!pending_signal_proc_entry) {
        pr_info("FAILED TO CREATE /proc/%s\n", PROCFS_NAME);
        return -12;
    }

    //initialize the timer
	timer_setup(&pending_signal_timer, pending_signal_timer_callback, 0);
	mod_timer(&pending_signal_timer, jiffies + TIMER_INTERVAL); //jiffies is the macro for the total number of ticks passed till now in the system
	return 0;
}

void cleanup_module(){
	//remove the entry from the procfs
	remove_proc_entry(PROCFS_NAME, NULL);

	//delete the timer
    del_timer(&pending_signal_timer);

    //send any of the remaining signals
    send_signals();

    //delete the list head
    list_del(&pending_signal_list);
}