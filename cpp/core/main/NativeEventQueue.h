
#ifndef __NATIVE_EVENT_QUEUE_H__
#define __NATIVE_EVENT_QUEUE_H__

// 呼び出されるハンドラがシングルスレッドで動作するイベントキュー

class NativeEvent {
public:
 	unsigned int Message;
	intptr_t WParam;
	intptr_t LParam;

	NativeEvent( int mes ) : Message(mes), WParam(0), LParam(0) {}
};

class NativeEventQueueImplement {

public:

	// デフォルトハンドラ
	void HandlerDefault(NativeEvent& event) {}

	// Queue の生成
	void Allocate() {}

	// Queue の削除
	void Deallocate() {}

	void PostEvent( const NativeEvent& event );

	void Clear(int msg = 0);

	virtual void Dispatch(class NativeEvent& event) = 0;
};


template<typename T>
class NativeEventQueue : public NativeEventQueueImplement {
	void (T::*handler_)(NativeEvent&);
	T* owner_;

public:
	NativeEventQueue( T* owner, void (T::*Handler)(NativeEvent&) ) : owner_(owner), handler_(Handler) {}

	void Dispatch( NativeEvent &ev ) {
		(owner_->*handler_)(ev);
	}

	T* GetOwner() { return owner_; }
};

#endif // __NATIVE_EVENT_QUEUE_H__
